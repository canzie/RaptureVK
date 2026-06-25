# Multi-Window (Multiview) Architecture

## Purpose

Lets a panel be torn off into its own **OS window** (separate top-level window, draggable to another monitor) and closed again at runtime — the "detach panel" feature in Blender/Unreal/Unity, built on real OS windows rather than in-app floating frames. See [[Editor Layout Design]] and [[Input and Camera Control Architecture]].

Process-wide rule: one `VkInstance` / `VkDevice` / VMA allocator / queue set for the whole app. What becomes plural is a tight cluster — *OS window + surface + swapchain + per-frame sync + present + input routing*.

Every statement below is taken from the current source; file:line references are given so the plan can be re-checked.

## Status

**Phase 1 — DONE** (`RenderWindow` extraction + device/window split, still single window, no behaviour change):
- New `window_context/RenderWindow.{h,cpp}` owns `{WindowContext, VkSurfaceKHR, SwapChain}`, creates its surface from the shared instance at construction, builds the swapchain via `createSwapChain()` once the device exists, and owns its resize→recreate listener.
- `VulkanContext` is now device-only: ctor does instance + debug messenger; new `initDevice(VkSurfaceKHR)` does physical/logical device + VMA with the surface **threaded as a parameter** through `pickPhysicalDevice`/`isDeviceSuitable`/`findQueueFamilies`/`querySwapChainSupport`/`createLogicalDevice` (no stored `m_surface`). Removed `m_surface`, `m_swapChain`, `getSurface()`, `getSwapChain()`, `createWindowsSurface()`, `createResources()`. `initManagers(uint32_t framesInFlight)` takes the count instead of reaching into the swapchain.
- `onRequestSwapChainRecreation` now carries a `uint32_t` swapchain id (`SwapChain::getID()`, ids from 1; 0 = invalid). The 3 publishers pass their swapchain id; each `RenderWindow` ignores ids that aren't its own — no cross-window recreation. No raw `SwapChain*` handed through the event.
- `Application` holds `m_mainWindow` (declared after `m_vulkanContext` so surface/swapchain tear down before device/instance). `getMainWindow()` added; `getWindowContext()` delegates to it. Callers in `Scene`/`Renderer`/`CascadedShadowMapping`/`AmethystLayer` route through `getMainWindow().getSwapChain()`.
- Minimized handling: the old blocking `waitEvents` loop in the recreate path is gone (it would stall every window). The recreate listener early-returns on zero framebuffer, and the present loop (`AmethystLayer::onUpdate`) now skips the frame when the framebuffer is zero-sized.
- Platform note: no Linux/Wayland/X11 functional code was removed — the runtime detection and surface **instance-extension** selection stay in `VulkanContext::createInstance`/`getRequiredExtensions`. Only the cosmetic per-platform log lines at surface-creation time were dropped (`glfwCreateWindowSurface` is platform-agnostic).

**Phase 2 — DONE (present loop lifted):**
- `RenderWindow::beginFrame()` acquires the next image and hands back an `AcquiredFrame { acquired, imageIndex, imageView, commandBuffer }` with the command buffer already begun (from a per-window present command pool, created lazily on first frame since managers don't exist at `createSwapChain` time). `RenderWindow::endFrame()` ends + submits + presents + advances, and on `OUT_OF_DATE`/`SUBOPTIMAL`/resize publishes the id-scoped recreation request.
- `AmethystLayer::onUpdate` is now record-only: `beginFrame` → tick UI + register viewport texture → `m_backend.record` into `frame.imageView` → `endFrame`. All acquire/semaphore/submit/present/frame-index state moved out of the editor; `beginDynamicRendering/endDynamicRendering` take the image index as a param.
- Minimized skip is now push-based: GLFW iconify callback sets `WindowContext::isMinimized()`; `beginFrame` early-returns on it (no per-frame `getFramebufferSize` poll). The per-window resize→recreate flag also moved onto `RenderWindow`.

**Phase 2 — remaining:**
- **DONE:** `onSwapChainRecreated` now carries a `uint32_t` swapchain id; `DeferredRenderer` and `AmethystLayer` filter to their own swapchain (`getMainWindow().getSwapChain()->getID()`).
- The `DeferredRenderer` `TargetType::SWAPCHAIN` path still has its own inline acquire/submit/present (runtime/no-editor mode, not active in editor) — unify it onto `RenderWindow::beginFrame/endFrame` once runtime mode is actually exercised.

**Phase 3 — DONE (per-window input):** smaller than first scoped, because `Input` (`input/Input.{h,cpp}`) already *polls* a specific `WindowContext` for keys/buttons/cursor, and the global key/mouse-button/mouse-move `InputEvents` have **no Rapture consumers** (Amethyst reads input via its own chained GLFW callbacks). The only global-event leak was scroll — now per-window: GLFW `scrollCallback` routes through the window user pointer and accumulates onto `WindowContext::m_scrollAccumulator`; `Input::onUpdate` reads `WindowContext::consumeScrollDelta()`. The minimized flag was already pushed the same way (iconify callback). The vestigial key/mouse `InputEvents` publishes are left in place (harmless, no consumers). Per-window window-close attribution stays with Phase 4 (no secondary windows exist yet).
**Phase 4 — DONE (window list + detach-on-the-fly + demo):**
- **GLFW platform lifetime split out**: new `window_context/PlatformContext.h` (abstract) / `GlfwContext.h/.cpp` (concrete: `glfwInit` in ctor, `glfwTerminate` in dtor), owned by `Application` as `std::unique_ptr<PlatformContext>`, declared before `m_vulkanContext`/`m_mainWindow`/`m_secondaryWindows` so it outlives every window. `WindowContext::createWindow` now takes a `PlatformContext&` and threads it into `GlfwWindowContext`. This replaced a real bug: every `GlfwWindowContext::closeWindow()` used to call `glfwTerminate()` unconditionally (fine with one window; closing *any* window while another was alive killed GLFW process-wide).
- `WindowContext` gained `getId()` (static counter, ids from 1, same pattern as `SwapChain::getId()`) and `shouldClose()` (wraps `glfwWindowShouldClose`).
- `onWindowResize` now carries a `windowId` (same shape as the swapchain-id events); `RenderWindow` and `DeferredRenderer` filter to their own window so resizing one window can't flip another's resize flag. `SwapChain`'s own resize listener was dead code (body fully commented out) and got removed outright rather than updated.
- `Application` holds `m_secondaryWindows` (`std::vector<std::unique_ptr<RenderWindow>>`) and exposes `createSecondaryWindow(w, h, title)`. `RenderWindow` gained public `onFrame(RenderWindow&)`/`onClose()` `std::function` members — generic hooks so Engine never has to know about Amethyst/UI types. `run()` invokes `onFrame` per secondary window per frame, and on close calls `onClose()` immediately before erasing the entry.
- **Lifecycle model is intentionally not per-window-refcounted**: the main window is authoritative. If it closes, every secondary window closes in the *same* loop iteration (`!m_running` is itself a close condition alongside that window's own `shouldClose()`), then the GLFW platform is torn down exactly once, after every window is already gone.
- `m_vulkanContext->waitIdle()` runs before any closing window's swapchain/sync objects are destroyed — skipping this crashes validation (`vkDestroySemaphore`/`Fence`/`vkDestroySwapchainKHR` "currently in use by VkQueue"), since `submitAndFlushQueue` doesn't block and that window's last frame may still be in flight.
- **Footgun found during testing**: `RenderWindow::createSwapChain()` constructs the `SwapChain` object but does **not** build the actual `VkSwapchainKHR`/sync objects — the caller must also call `swapChain->invalidate()` (the main window's path buries this one call inside `Application`'s constructor, easy to forget for a second window; doing so was the cause of an out-of-bounds crash on `m_semaphoreIndexToFrameIndexMap`).
- Dropped a present-support assertion that was originally planned for new surfaces: `vkGetPhysicalDeviceSurfaceSupportKHR` for a physical device tests "can this GPU present via this display server at all," not "to this specific monitor" — since every detached window shares the main window's GLFW/display-server session, the answer is already proven by the main window working. Not a meaningful per-window check for the same-session multi-monitor case this feature targets.
- Demo: a "New Window" action under the editor's Window menu opens a real second OS window with a centered label, via `AmethystLayer::openDemoWindow()`. See the Amethyst section below — it required real (and different-than-planned) library work, not just an editor-side wiring.

## Run modes

There are two run modes, and the window/present layer must be **optional** so the engine spans both:

**1. Editor (client).** Multiview lives here. Assume a single canonical **main window** created first that lives the whole session; every other window is a transient *secondary* detached from it. This removes the hardest sub-problem — there is **no windowless device bootstrap in editor mode**. The existing init order already creates the main surface before the device (`VulkanContext.cpp:136-142`), so device/queue selection stays as-is and only needs to hold for the main window:
- `getWindowContext()` → `getMainWindow()` is a safe rename; editor code that targets "the window" keeps meaning the main one.
- Present-queue family is picked once against the main surface; secondaries only **assert** compatibility, never re-pick.
- Closing the main window ends the session and tears down all secondaries.

**2. Headless / server.** Must run with **no window, no surface, no swapchain, no present — and no graphics at all** (pure ECS + physics + scenes for a sim/training server). This is *not* the same as `SwapChain`'s existing `RenderMode::OFFSCREEN` (`SwapChain.h:18`), which still assumes a Vulkan device; headless wants the option of **no `VkInstance`/`VkDevice` whatsoever**. Relates to [[Sim engine branch idea]] and the postponed [[project_decoupling_pass2]].

### What headless requires (separate from multiview, but the same seam)
Today this is impossible because `Application::Application` (`Application.cpp`) creates `WindowContext` **and** `new VulkanContext(m_window.get())` **and** `initManagers()/createResources()` unconditionally, and `VulkanContext`'s ctor takes a `WindowContext*` for its surface. To support headless:
- **`Application` gains a mode** (e.g. `EDITOR` / `HEADLESS`) and constructs the window + graphics layer only for client modes. The `RenderWindow` extraction above is what makes "no windows" expressible — headless simply holds an empty window list and never builds a `VulkanContext`.
- **Engine core must not transitively require `VulkanContext`/`WindowContext`.** This is the real cost and is the same blocker tracked in [[project_decoupling_pass2]] / [[project_shadow_component_antipattern]] — GPU work currently reachable from components/scenes must move behind the render layer so a Scene can tick with no device present. *(Extent of this coupling is an open item — see below; it must be audited, not assumed.)*

So the layering the whole plan drives toward: **engine core (no graphics) → optional Vulkan device layer (`VulkanContext`) → optional presentation layer (`RenderWindow` × N).** Editor uses all three; the server uses only the first.

## Current state — verified

| Concern | Evidence | Singular today? |
|---------|----------|-----------------|
| OS window | `GlfwWindowContext` creates one `m_glfwWindow` (`GlfwWindowContext.cpp:42`) | yes |
| Surface | `VkSurfaceKHR m_surface` (`VulkanContext.h:131`), created in `createWindowsSurface` (`VulkanContext.cpp:1321`) | yes |
| Swapchain | `std::shared_ptr<SwapChain> m_swapChain` (`VulkanContext.h:126`) | yes |
| Per-frame sync | semaphores/fences live in `SwapChain` (`SwapChain.h:93-95`) | per-swapchain already |
| Present loop | acquire/submit/present hardwired to the one swapchain (`AmethystLayer.cpp:190,268,282`) | yes |
| Window accessor | `Application::getWindowContext()` returns the one `m_window` (`Application.h:28,45`) | yes |
| Resize event | `SwapChain` subscribes to global `ApplicationEvents::onWindowResize` (`SwapChain.cpp:28`) | global, no window id |

Already decoupled / reusable as-is:
- `SwapChain` ctor takes `(device, surface, physicalDevice, queueFamilyIndices, windowContext)` (`SwapChain.h:30`) — it does not assume it is "the" swapchain.
- **Viewports render offscreen**: `Viewport` owns a `Renderer` and a `SceneRenderTarget` exposed via `getSceneRenderTarget()` (`Viewport.h:54`); the UI samples that texture (`AmethystLayer.cpp:215-222`). Its only swapchain tie is `Viewport::onSwapChainRecreated()` → `m_renderer->onSwapChainRecreated()` (`Viewport.cpp`). So showing a viewport in another window needs no render changes. See [[SceneRenderData]].
- **Queue submission is thread-safe**: every `VulkanQueue` op (`submitQueue`, `submitAndFlushQueue`, `flush`, `presentQueue`, `waitIdle`, `clear`) takes `std::lock_guard(m_queueMutex)` (`VulkanQueue.cpp:127,208,57,302,308,314`), and it exposes `acquireQueueLock()` (`VulkanQueue.h`). Multi-window submits through the shared queue are therefore safe; the only cost is contention, not correctness.
- Instance / physical+logical device / queues / VMA / managers in `VulkanContext` are process-wide and stay singletons.

## Target architecture

Extract the per-window cluster into one new presentable-window type. Working name **`RenderWindow`** (folder `window_context/`), owning:

```
RenderWindow
  ├─ WindowContext        (GLFW window + input)
  ├─ VkSurfaceKHR         (from the shared VkInstance)
  └─ SwapChain            (surface + per-frame sync + present)
```

`VulkanContext` is trimmed to the shared device layer. `Application` holds a list of `RenderWindow`s (main + detached); `run()` iterates them. This mirrors Dear ImGui's multi-viewport `ImGui_ImplVulkanH_Window` (per-window bundle, created/destroyed as viewports appear) and its `RenderPlatformWindowsDefault()` per-window acquire→submit→present loop.

## File-by-file changes (core engine)

### `window_context/RenderWindow.{h,cpp}` — NEW
Owns `{ WindowContext, VkSurfaceKHR, SwapChain }`. API roughly: `create(VulkanContext&, w, h, title)`, `beginFrame()/present()`, `windowContext()`, `swapChain()`. Dtor tears down swapchain → surface → GLFW window.

### `VulkanContext.{h,cpp}`
- **Move out** to `RenderWindow`: `m_surface` (`.h:131`), `m_swapChain` (`.h:126`), `getSurface()` (`.h:29`), `getSwapChain()` (`.h:33`), `createWindowsSurface()` (`.h:112` / `.cpp:1321`).
- **Keep**: instance, physical/logical device, queues, VMA, managers, extension fn pointers, `RenderContext`.
- **Bootstrap ordering is load-bearing**: init runs `createWindowsSurface` (`.cpp:136`) → `pickPhysicalDevice` (`.cpp:138`) → `createLogicalDevice` (`.cpp:139`, which calls `findQueueFamilies` at `.cpp:1203`) → `SwapChain` (`.cpp:142`); and `findQueueFamilies` selects the PRESENT family via `vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, …)` (`.cpp:677`). So the **main window's surface must be created before device creation** to pick the present queue. Keep that bootstrap (main surface first), then assert present support for each *additional* surface at its swapchain creation rather than re-picking queues.

### `SwapChain.{h,cpp}`
- Replace the global resize subscription `ApplicationEvents::onWindowResize().addListener(...)` (`.cpp:28`, removed at `.cpp:41`) with a **per-window** resize signal driven by its owning `RenderWindow`; otherwise a resize on one monitor recreates another window's swapchain.
- The recreation request it publishes, `ApplicationEvents::onRequestSwapChainRecreation()` (`.cpp:297`), must likewise carry/scope to its window.
- Sync objects, `recreate()`, `invalidate()`, `acquireImage`/`presentImage` are already per-instance — only ownership moves.

### `Application.{h,cpp}`
- `std::unique_ptr<WindowContext> m_window` (`.h:45`) → a list of `RenderWindow` (index 0 = main).
- `run()` (`Application.cpp`): currently one pass ending in `m_window->onUpdate()`. Generalize to `glfwPollEvents` once, then per window drive its layers/UI and `beginFrame…present`.
- `getWindowContext()` (`.h:28`) is ambiguous under N windows → rename to `getMainWindow()` or make callers name the window. **Audit all `getWindowContext()` callers** (e.g. `EditorLayer.cpp:35` constructs `Input` from it; `AmethystLayer.cpp:78,122`).
- Window-close: today `windowCloseCallback` fires a single global `ApplicationEvents::onWindowClose()` (`GlfwWindowContext.cpp:165`). Make it identify the window — closing a detached window destroys that `RenderWindow`; closing the main window exits.

### `events/ApplicationEvents`
- **DONE:** `onRequestSwapChainRecreation` now carries a `uint32_t` swapchain id; each `RenderWindow` filters to its own.
- **Phase 2:** `onWindowResize`, `onWindowClose`, `onSwapChainRecreated`, `onWindowFocus/LostFocus` are still global with no window id (publish sites: `GlfwWindowContext.cpp:165,180,234`). Add a window identifier (or move to per-`RenderWindow` signals).

### `GlfwWindowContext.cpp` + `Input`
- `glfwSetWindowUserPointer(m_glfwWindow, this)` is already set (`:49`), but **input callbacks ignore it**: `keyCallback`, `charCallback`, `mouseButtonCallback`, `cursorPosCallback`, `scrollCallback` all do `(void)window;` and publish to global `InputEvents::onX()` singletons (`:183-238`); only `windowSizeCallback` reads the user pointer (`:175`). For per-window input, route each callback through `glfwGetWindowUserPointer` to its `WindowContext`, and have `InputEvents` carry a window id (or `Input` poll a specific context). `Input` already takes a `WindowContext*` so it is per-window-ready (`EditorLayer.cpp:35`). The focus router decides the active window each frame (extends [[Input and Camera Control Architecture]]).

### Viewport / ViewportManager
- No rendering change (offscreen → texture). The single coupling is `Viewport::onSwapChainRecreated()` → `m_renderer->onSwapChainRecreated()`; once swapchain-recreated events are per-window, route the recreation of the window that hosts the viewport. A viewport texture is registered with a UI backend per window (Amethyst section).

## Lifecycle

**Create on the fly** (`Application::createSecondaryWindow`, actual implemented flow):
1. `WindowContext::createWindow(platformContext, w, h, title)` → new `GlfwWindowContext`, sharing the process-wide `PlatformContext` — no `glfwInit`/`glfwTerminate` here.
2. `RenderWindow` ctor creates the surface from the shared `VkInstance`.
3. `RenderWindow::createSwapChain()` **followed by an explicit `swapChain->invalidate()`** — see the footgun note above; the swapchain doesn't actually exist until `invalidate()` runs.
4. Caller (e.g. `AmethystLayer::openDemoWindow`) builds the window's UI root and sets `RenderWindow::onFrame`/`onClose`.
5. `createSecondaryWindow` pushes the new `RenderWindow` into `m_secondaryWindows` and returns a reference.

**Destroy on the fly** (window closes, or the main window closes and cascades):
1. `Application::run()` waits for the device to be idle (`m_vulkanContext->waitIdle()`) before touching anything.
2. Calls that window's `onClose()` — lets the owner (e.g. `AmethystLayer`) release its Amethyst-side state while the `RenderWindow` is still valid.
3. Erases the entry from `m_secondaryWindows` — `RenderWindow`'s destructor tears down swapchain → surface → `GlfwWindowContext` (→ `glfwDestroyWindow`, no `glfwTerminate`) in that order.
4. If this is the main window closing, every secondary window goes through 1-3 in the same loop iteration, then `PlatformContext` (and its owned `GlfwContext`) destructs once, after every window is already gone.

## Floating placement on tiling Wayland compositors (investigated, deferred)

Goal: on tiling Wayland compositors (Hyprland confirmed), a detached window should open floating rather than getting tiled into the layout, the way Blender's file-browser/render windows do — without requiring the user to hand-write a compositor window rule.

Verified directly from Hyprland's source (`src/managers/XWaylandManager.cpp`, `CHyprXWaylandManager::shouldBeFloated`) — for native Wayland clients it auto-floats a window if **either**:
- its `xdg_toplevel` has a parent set (`xdg_toplevel_set_parent`), or
- its min/max size are equal on an axis (non-resizable on that axis).

The parent-relationship path is how Blender does it (GHOST talks to Wayland directly, not through a windowing library) and is independent of resizability — the "right" mechanism. It requires holding the actual `xdg_toplevel` proxy object for *both* the child and parent window. GLFW creates and owns both internally for every window it makes and never exposes either, not via `glfw3native.h`, not via any wrapper (`grep`-verified in the vendored GLFW 3.4 source: `wl_window.c` never calls `set_parent`, and the toplevel pointer doesn't appear in the native-access header at all). There is no Wayland-protocol way to retrieve an object's existing role-proxy from outside the library that created it. So this is achievable only by:
1. Patching GLFW (vendored or otherwise) to expose a `set_parent` call, or
2. Bypassing GLFW's window management for the windows that need parenting — which hits the same wall for the *main* window's toplevel, so it would mean moving main-window creation off GLFW too.

Both are bigger than this feature warrants today. **Decision: defer, build a custom (non-GLFW) Wayland windowing path later** — `WindowContext`/`GlfwWindowContext` already isolate all GLFW specifics behind an abstract interface (see Phase 1-4 above), so swapping in a Wayland-native implementation for Linux later should be comparatively low-risk; it would own its surfaces/toplevels directly and could set the parent relationship for real.

**Fallback that does work today, kept as inert code**: a window hinted non-resizable at creation (`glfwWindowHint(GLFW_RESIZABLE, false)`) reports `min == max` on first map, which trips Hyprland's size-based heuristic and floats it; calling `glfwSetWindowAttrib(window, GLFW_RESIZABLE, true)` *after the window's first actual present* (not right after `glfwCreateWindow` — the role-assignment commit during creation has no buffer attached yet and isn't what the compositor uses to decide floating vs. tiled) restores genuine resizability while the floating placement sticks (it's a one-time decision at first map, not continuously re-evaluated). Confirmed working in practice. Side effect: the window didn't respect its requested size (480×270) and landed at roughly half the screen — Hyprland's default-floating-size fallback for a window it saw as fixed-size at map time, presumably. `WindowContext::createWindow`'s `preferFloating` parameter is kept threaded through (`GlfwWindowContext::m_preferFloating`) but currently does nothing — reimplement by re-adding a `WindowContext::onFramePresented()` hook (called from `RenderWindow::endFrame()` after `presentQueue`) that does the deferred `glfwSetWindowAttrib` restore, if the custom-Wayland-path effort stalls or gets deprioritized.

## Open questions (not yet investigated)
- Whether `Renderer::onSwapChainRecreated()` assumes a single global swapchain's image count (affects per-window frames-in-flight). Still untested — the demo window is pure Amethyst UI with no 3D viewport, so `DeferredRenderer` never runs against a secondary window in this work.
- Policy for one viewport shown in two windows simultaneously (share one texture vs. one viewport per window). Still open.
- ~~Minimized / zero-size detached windows~~ — **resolved**: `RenderWindow::beginFrame()`'s existing `m_windowContext->isMinimized()` early-return already generalizes correctly; `drawSecondaryWindow` uses it as-is, no special-casing needed.
- Viewport textures are currently sized/cleared off the *window's* swapchain image count on every resize, but they should track the viewport panel's own size and only change when that panel actually resizes — flagged with a `TODO` in `AmethystLayer::onResize()` rather than fixed now.
- GLFW input routing for secondary windows isn't wired up (the demo window has nothing interactive). `AmVulkanBackend`'s single `m_glfwInfo.uiWindow`/content-scale fields would need to become a per-`GLFWwindow*` map — same shape as the per-window draw-list fix below — before a detached window could host real interactive widgets.

## Out of scope
- In-app floating frames already exist via Amethyst's `DockingLayer` and need none of the above. This doc is strictly the separate-OS-window path.
- Drag-to-split (tear a panel out into a new OS window by dragging) and drag-to-join/auto-merge (drag a detached window's content back into the main docking layout). The current demo only has a static "New Window" menu action with no docking integration at either end.

---

## Amethyst side — DONE (shared backend/context, not per-window)

The original sketch above assumed each detached window would need its own `AmVulkanBackend` + `AmethystContext` ("one per window is simplest"). That turned out to be wrong, and the actual fix was smaller: per detached `RenderWindow` the editor only needs a new `Amethyst::Window` UI root. The pipeline, descriptor pool/set, font, glyph/SVG atlas, and vertex/index buffers are all **shared** through the *same* `AmVulkanBackend`/`AmethystContext` the main window already owns.

Why sharing is correct: nothing in Amethyst or this present path is multithreaded, and every window's frame is drawn → recorded → submitted sequentially, one window fully done before the next starts. There's no concurrency hazard in sharing GPU state across windows; the question was only ever which *mutable per-frame* state would get clobbered by a second window reusing it, and that turned out to be exactly two things:

1. **The draw list.** `GpuResourceHub`/`AmethystContext::sync()` split into `syncShared()` (atlas texture uploads + the shared gradient buffer — call once per frame) and `syncWindow()` (per-window registry sync + draw list build — call once per window). The per-window filter walks each `GeometryRegistry`'s owning `UILayer` up the existing `Instance::parent` chain to its root `Window` — no new bookkeeping needed, since every registry already gets its own non-overlapping suballocated block in the shared arenas (`GpuResourceHub::obtainGeometryBlocks`, keyed by registry pointer already). `GpuResourceHub::m_drawLists` is now a `std::unordered_map<Window*, FrameDrawList>` instead of one shared list.
2. **The record-time viewport extent.** `AmVulkanBackend::record()` now takes the target window's `VkExtent2D` as a parameter instead of reading a single shared `m_info.extent` that used to be set by an `onResize()` method (now removed). The old design silently corrupted whichever window *didn't* most recently resize — one mutable member can't hold two windows' sizes at once. All call sites (the editor and all 6 `amethyst_testapp` demos) updated to pass their own extent.

`AmethystLayer::SecondaryWindowContext` ended up as just `{RenderWindow*, Amethyst::Window}`. `openDemoWindow()` builds a small UI tree (background frame + centered label) under a fresh `Amethyst::Window` and draws/records it through the *existing* `m_backend`/`m_amCtx`, with no separate descriptor pool, font load, or backend init.

**Not done**: GLFW input routing for secondary windows. `AmVulkanBackend`'s single `m_glfwInfo.uiWindow` field and the `s_backendForWindow` dispatch map only ever register one `GLFWwindow*`; a second window's input would either go nowhere (if never registered, today's behavior — the demo window has nothing interactive) or get misrouted (if naively registered without also making `uiWindow`/content-scale per-`GLFWwindow*`). Needed before any detached window can host real interactive widgets.

### Amethyst per-window input — DONE (at the core level; not wired end-to-end)

Written before the backend became shared across windows. Still accurate for what it covers — `InputInterface` already routes to a specific target `Amethyst::Window*` — but on its own this doesn't deliver input to a second window, since that requires a `GLFWwindow*` to actually be registered against a backend (see "Not done" above). An `Amethyst::Window` is a UI root, **not** an OS window; the original design assumed each OS window (`RenderWindow`) would pair one `GLFWwindow` + one `AmVulkanBackend` + one `Amethyst::Window` — true for the main window, not extended to secondaries.

- **Core (`libamethyst`) stays glfw-free.** `InputInterface`'s mouse methods now take a target `Amethyst::Window *` and route to that one window instead of broadcasting to all (`onMouseMove/onMouseButton/onMouseScroll(Window*, …, x, y)`). Removed the global `s_mouseX/Y`. Double-click stays a single global state plus an `s_lastClickWindow` guard (a click can't span windows). Keyboard/char/clipboard/cursor-shape/lock remain **global** (focus-delivered) — shortcuts unaffected.
- **Backend (glfw, the only glfw-aware layer) owns the native↔window mapping.** Removed the global `g_glfwData`; prev-callback chain + content scale are per-instance members, keyed by a `static unordered_map<GLFWwindow*, AmVulkanBackend*>`. Mouse callbacks resolve the backend, scale by that window's content scale, fetch cursor pos via `glfwGetCursorPos` for button/scroll, and call `InputInterface::onMouse*(m_glfwInfo.uiWindow, …)`.
- **Registration**: `AmGlfwInitInfo` gains `Amethyst::Window *uiWindow`; the integrator pairs them at `backend.init` (`AmethystLayer` sets `glfwInfo.uiWindow = &m_window`). Core never sees a native handle.
- **Decision**: only **mouse position** is per-window (buttons/keys filter by position anyway). Verifiable single-window — behaviour identical with one window.
- **Phase-4 follow-up**: the cursor-shape/lock/clipboard callbacks are global single-assignment capturing the last-init'd window; with multiple windows they should target the **focused** window. Keyboard/text per-window only needed if a detached window ever hosts text input.
