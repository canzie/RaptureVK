# Multi-Window (Multiview) Architecture

## Purpose

Lets a panel be torn off into its own **OS window** (separate top-level window, draggable to another monitor) and closed again at runtime — the "detach panel" feature in Blender/Unreal/Unity, built on real OS windows rather than in-app floating frames. See [[Editor Layout Design]] and [[Input and Camera Control Architecture]].

Process-wide rule: one `VkInstance` / `VkDevice` / VMA allocator / queue set for the whole app. What becomes plural is a tight cluster — *OS window + surface + swapchain + per-frame sync + present + input routing*.

Every statement below is taken from the current source; file:line references are given so the plan can be re-checked.

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
- `onWindowResize`, `onWindowClose`, `onSwapChainRecreated`, `onRequestSwapChainRecreation`, `onWindowFocus/LostFocus` are global with no window id (publish sites: `GlfwWindowContext.cpp:165,180,234`; `SwapChain.cpp:297`; consumed `AmethystLayer.cpp:39,42`). Add a window identifier (or move to per-`RenderWindow` signals).

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
