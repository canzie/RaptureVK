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

**Phase 2 — remaining / Phase 3+:**
- **DONE:** `onSwapChainRecreated` now carries a `uint32_t` swapchain id; `DeferredRenderer` and `AmethystLayer` filter to their own swapchain (`getMainWindow().getSwapChain()->getID()`).
- The `DeferredRenderer` `TargetType::SWAPCHAIN` path still has its own inline acquire/submit/present (runtime/no-editor mode, not active in editor) — unify it onto `RenderWindow::beginFrame/endFrame` once runtime mode is actually exercised.
- `Application::run()` still drives the present indirectly through the `AmethystLayer` overlay; moving orchestration to a per-window loop in `run()` comes with Phase 4 (multiple windows).
- Per-window input routing (Phase 3) and detach-on-the-fly + Amethyst per-window root (Phase 4).

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

**Create on the fly** (panel dragged out):
1. `glfwCreateWindow(...)` → new `WindowContext`.
2. `glfwCreateWindowSurface(sharedInstance, window, …)` → surface (same call as `VulkanContext.cpp:1332`).
3. `SwapChain(device, surface, physicalDevice, queueFamilyIndices, windowContext)` — assert `vkGetPhysicalDeviceSurfaceSupportKHR` for the present family.
4. Build the window's UI root + backend (Amethyst section).
5. Push the `RenderWindow` into `Application`'s list.

**Destroy on the fly** (drag back / close):
1. Gate on that window's in-flight fences (or `vkDeviceWaitIdle` for v1 — note `AmethystLayer::onResize` already does `waitIdle` before swapchain churn, `AmethystLayer.cpp:528`).
2. Tear down swapchain → surface (`vkDestroySurfaceKHR`, cf. `VulkanContext.cpp:186`) → UI → `glfwDestroyWindow`.
3. Erase from the list.

## Open questions (not yet investigated)
- Whether `Renderer::onSwapChainRecreated()` assumes a single global swapchain's image count (affects per-window frames-in-flight). Needs a read of the renderer before implementing.
- Policy for one viewport shown in two windows simultaneously (share one texture vs. one viewport per window).
- Minimized / zero-size detached windows: the loop must skip their frame (generalize whatever single-window minimize handling exists in `Application::run`).

## Out of scope
- In-app floating frames already exist via Amethyst's `DockingLayer` and need none of the above. This doc is strictly the separate-OS-window path.

---

## Amethyst side (deferred — sketch only)

The UI library is already decoupled from the swapchain: `AmVulkanInitInfo` takes `device/colorFormat/imageCount/extent` (`AmethystLayer.cpp:107-119`) and `m_backend.record(cmd, drawList)` renders into whatever image view is handed to it (`AmethystLayer.cpp:251-255`). It never touches surface/swapchain/present. Its only OS coupling is `AmGlfwInitInfo.window` (one `GLFWwindow*`, `:122`) and the per-window UI root `Amethyst::Window` (`m_window`).

So per detached `RenderWindow` the editor needs: a new `Amethyst::Window` root (already standalone), the moved panel subtree reparented into it, a UI backend instance pointed at that window's swapchain format/extent (one per window is simplest) with its own glfw input hookup, and per-window texture registration for any viewport shown there. Full Amethyst-side design to follow once `RenderWindow` lands.
