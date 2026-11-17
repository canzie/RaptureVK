# Engine Hardening

Review of `Engine/src` as of 2026-11-15 (branch `editor-restructure`), and the work list that comes out of it. Spans several sessions; tick items off here as they land. Every finding is checked against the tree and cited.

Scope of the read: app and platform, the queue and command pool layer, `DrawManager` and the deferred renderer, DDGI, the scene object model, `Scene`/`World`, Lua scripting, the asset manager and eviction, the custom ECS in `core/ecs`, jobs and fibers, physics, and the CMake setup. Not read line by line: individual passes, textures, glTF, terrain, serialization. Those are covered by the stress and test work rather than by review.

## Scores

Out of 10. The architecture is ahead of its implementation: the risk is concentrated in GPU synchronization, in state owned at the wrong level (per scene vs per view), and in lifetime edges the happy path never reaches.

| Area | Score | Why |
|---|---|---|
| Extensibility | 6 | New renderers slot into `DrawManager` by phase and order (`DrawManager.h:71`, `:209`); scene object classes and asset types are table/registry driven. Inside `DeferredRenderer` a pass costs four edits (member, command buffer pointer, job in `recordSecondaries`, block in `replay`) with hand-written barriers, which is local to that renderer. |
| Scalability | 4 | Per-scene thread pools, shadows and terrain following one camera per scene, GI duplicated per view, 12-bit entity generations. See "Ownership and scale". |
| Maintainability | 6 | Consistent style and docs. `Scene` owns too much (registry, physics, TLAS, render data, Lua runtime, ticking, shadow views, terrain). Two ways to create content (raw-ECS `Scene::createCube`/`createSphere` and scene objects), two present paths (`DrawManager::submitFrame` and `RenderWindow::endFrame`), 53 `throw`s in `gpu/` against the no-exceptions rule. |
| Performance | 6 | Good foundations: MDI, bindless, arena buffer pools, jobified secondary recording, sparse-set ECS with a change journal. Costs: the thread count, `waitIdle` on every viewport resize and destroy, DDGI per view, the per-frame material sweep. |
| Elegance | 6 | Asset eviction (deferred buckets, never evicts while loading, VRAM-driven cold list), fixed-step physics with a step cap, and Lua handles with alive flags are well thought through. The graphics queue `clear()` on acquire failure and thread-id keyed command pools under fibers are patches that will bite. |
| Correctness | 4 | See "Bugs". |
| Portability | 3 | Fibers are SysV-only assembly; no queue family ownership transfers anywhere. |
| Testability | 3 | GoogleTest is already in place (`Engine/test/`, `Editor/test/`), but `Application`'s constructor creates the window and device, so little above `core/` can be built in a test. Same work as the windowless run. |
| Library hygiene | 4 | Include root is `src/` so every header is public; vendor libraries link `PUBLIC`; `add_definitions` is global; `-Werror` only in Debug (`Engine/CMakeLists.txt`). |

Statics for `Application`, `AssetManager` and the job system are intentional and not counted against anything.

## Bugs

Ordered by how likely they are to be hit.

- [x] **Asset unload null dereference.** `AssetManagerEditor::processUnloadRequests` null-checks `slot->asset` before the status check but then calls `slot->asset->useCount()` unconditionally (`AssetManagerEditor.cpp:795`). Every last release enqueues the handle (`Asset.cpp:23`), so an asset released to zero twice in one frame is dequeued twice: the first evicts it (non-virtual assets keep their slot with a null payload), the second dereferences null. `EVICT_IMMEDIATE` is the default policy. First unit test to write.
- [ ] **Fibers crash on Windows.** The context switch is SysV x86-64 assembly (`Fiber.cpp:38`): arguments in rdi/rsi and only the SysV callee-saved registers. Win64 passes in rcx/rdx, also preserves rdi, rsi and xmm6-15, and needs the TIB stack base/limit swapped, or stack probes and SEH fail. Use `CreateFiber`/`SwitchToFiber` on Windows, or Win64-correct assembly.
- [x] **DDGI races the lighting pass.** The probe update is submitted on the compute queue with no semaphores (`DynamicDiffuseGI.cpp:353`) while the graphics queue samples the same images. Latent today: the queue family bug below puts every role on family 0, one `VkQueue` (development GPU log: graphics, compute, present and transfer all `0:16`), so submission order hides it. Fixing the family selection makes it real. Fixed by keeping DDGI on the graphics queue: its inputs (TLAS, light slots, mesh and material buffers) are rewritten by graphics every frame, so async compute would need graphics to wait on it before every such write, or copies of those inputs. The probe textures are double buffered and published per update, so moving it later only needs its inputs owned. The terrain culler, terrain generator and procedural textures moved to graphics for the same reason; the compute role has no users.
- [x] **No queue family ownership transfers.** Every buffer and image is `VK_SHARING_MODE_EXCLUSIVE` and no barrier anywhere sets a real `srcQueueFamilyIndex`. Latent for the same reason as the DDGI race: every role is family 0 today. Has to land together with the family selection fix. Fixed with sharing instead of transfers: buffers, acceleration structures and every image not marked `graphicsQueueOnly` are `CONCURRENT` across the families in use (`VulkanContext::shareAcrossQueueFamilies`), render targets stay `EXCLUSIVE`. Async texture uploads copy on the transfer queue and finish (mips, final layout) on the graphics queue behind a timeline wait. `DEDICATED_FAMILIES` is now the default and the GPU tests run under it, including an async upload test.
- [x] **A failed acquire discards batched work.** `RenderWindow::beginFrame` calls `getGraphicsQueue()->clear()` when acquire fails (`RenderWindow.cpp:81`), which drops everything batched so far: offscreen viewport frames (`DrawManager.cpp:396`), BLAS builds (`AccelerationStructureBuilder.cpp:259`, `:313`) and IBL bakes (`ImageBasedLighting.cpp:271`). A dropped BLAS build leaves the TLAS referencing an unbuilt BLAS. Hit on window resize. Fixed: the `clear()` is gone. `VulkanQueue` reserves a batch's timeline value when its first buffer is added, so dropping a batch would strand that value; `clear()` no longer exists.
- [x] **Destroying during a tick frees a running object.** `Scene::destroyInstance` removes immediately (`Scene.cpp:333`) while `runTickPhase` iterates the live list (`Scene.cpp:316`). Not reachable yet because Lua cannot destroy; it will be the moment it can. Destruction needs deferring to the end of the phase. Fixed: a destroy requested while `runTickPhase` runs is queued and carried out once the phase's loop is done; outside a phase it stays immediate. Tested in `Engine/test/gpu/scene_destroy_test.cpp`, including a child destroying its parent and itself.
- [x] **Entity generation wraps.** 12 generation bits (`core/ecs/common.h:18`) with LIFO slot reuse: one spawn and destroy per frame wraps a slot in about 68 s at 60 fps, after which a stale `Entity` validates. Accepted for now (2026-11-17): EnTT uses the same 20/12 split, LIFO reuse and wrapping. A stale handle only validates if it is kept past 4096 reuses of its slot and checked while the slot's generation matches it again, which needs a dangling handle to begin with. FIFO reuse with a minimum free count, or retiring a slot instead of wrapping, stay available if it ever bites.
- [x] **Queue batch read without its lock.** `VulkanQueue::submitAndFlushQueue` (`VulkanQueue.cpp:205`, already a TODO). Fixed: `submitAndFlushQueue` is gone. `submitQueue` takes both locks and always sends the pending batch ahead of its own buffer, so the batch now runs before the frame that samples it, not after.
- [x] **Command pools keyed by thread id under fibers.** `DynamicDiffuseGI.cpp:294`, `SkyboxPass.cpp:70`, `Texture.cpp:66`, `:329`, `:1690`. Two jobs interleaving on one worker share a pool whose bookkeeping is non-atomic. Replaced by `CommandBufferLease`: a recording holds a pool to itself until it drops the lease, and a returned pool is reused only once the GPU has passed every submission of it, so a recording may yield and resume on another worker. Secondaries move into the primary that executes them and go back with its submission.
- [x] **`Texture::setPixel` and `setPixels` free their staging buffer right after submitting.** `vmaDestroyBuffer` follows `submitQueue` with no wait (`Texture.cpp`, the comment above it claims a pool reset recycles it, which no pool ever did). The copy can read freed memory. Fixed: the staging buffer is released by a job that waits on the timeline value the submit returns. Neither function has a caller.
- [x] **Component type ids across modules.** `NextComponentTypeId` is a non-atomic static in an inline function (`component_pool.h:22`). Games never define components, but a C++ scripting module that instantiates engine ECS templates inline (any scene object accessor that reaches `registry.read<T>`) gets its own counter and disagrees with the engine on ids. Matters only once C++ scripting exists. Fixed: `ComponentType<T>` caches the id the engine hands out for T's compiler generated signature (`ComponentTypeIdForSignature`, one locked table in `registry.cpp`), so a module instantiating the templates gets the engine's ids. A module built with a different compiler spells signatures differently and would not match, which a C++ module cannot do anyway.
- [x] **Queue family selection only treats graphics as unset.** `QueueFamilyIndices::familyIndices[COUNT] = {UINT32_MAX}` (`VulkanTypes.h:20`) sets element 0 only; compute, present and transfer start at family 0. The `== UINT32_MAX` branches in `findQueueFamilies` (`VulkanContext.cpp:640-690`) never run for them, the present branch writes `GRAPHICS` where it means `PRESENT` (`:682`), and `isComplete` passes on a device whose family 0 cannot compute or present. Works today because family 0 does everything on the development GPU. Also hides the two items above: since every role is family 0, the engine runs on a single queue. Fix together with them in the GPU synchronization step, not alone. Fixed: every role starts unset and is picked on purpose (graphics+compute family, compute-only, plain transfer-only, present on graphics when it can). `ApplicationConfig::queueFamilyAssignment` chooses between `ONE_FAMILY` (the default until the cross-queue work lands) and `DEDICATED_FAMILIES`. Each queue now has one timeline that every submission signals in order, and submissions take timeline waits.
- [x] **A `Camera3D` renders from the origin unless a controller drives it.** The view matrix only exists in `CameraComponent::camera` and only controllers write it (`CameraController::updateViewCamera`, `PlayerController.cpp:86`); `SceneRenderData` reads it from there (`SceneRenderData.cpp:602`). `Camera3D::setPosition` moves the transform, which the view never reads. Found by the windowless smoke test, which drives its camera with a `CameraController` until this is fixed. The view should follow the camera's world transform. Fixed: `Node3D::onWorldTransformChanged` fires whenever a world transform is rebuilt, and `Camera3D` rebuilds its view from it. `Controller::updateViewCamera` is gone; `CameraController` turns its camera to its forward instead of writing the view, and `PlayerController`'s camera already follows the arm. The smoke test places its camera with `setPosition` and `lookAt`.
- [x] **A viewport only set its camera's aspect ratio on resize.** A viewport created at its final size, or handed a new camera, rendered with whatever aspect the camera had (`Viewport.cpp`, `updateCameraAspectRatio`). Found by the windowless smoke test.
- [x] **Latent: layer and window lists mutated while iterated.** `Application::run` iterates `m_layerStack` and `m_secondaryWindows` with iterators (`Application.cpp:198-230`); `pushLayer` inserts into the same vector. Clicks are dispatched from `glfwPollEvents`, outside those loops, so nothing hits it today. Shortcuts run inside `AmethystLayer::onUpdate`, inside the loop, so the first shortcut that pushes a layer will. Fixed: `LayerStack::updateLayers`/`updateOverlays` run the loops, and pushes and pops made during them are applied once the loop is done. Secondary windows are walked by index, so a callback that opens a window does not invalidate the loop.

## Ownership and scale

What is owned at the wrong level. The per-asset / per-scene / per-view rule is already decided in [[ECS Tiers and Ownership]]; these are the places that do not follow it yet.

- [x] **Physics thread pool per scene.** Every `Scene` builds a `PhysicsSystem` (`Scene.cpp:56`) and each initialises a Jolt `JobSystemThreadPool` with `hardware_concurrency - 1` threads (`PhysicsSystem.cpp:127`), preview scenes included. One shared physics job system, or Jolt running on the engine's job system. Fixed: `physics::JoltFiberJobSystem` runs Jolt's jobs as engine jobs, one for every `PhysicsSystem`, living as long as the Jolt globals. Jolt jobs never wait inside a job, only the thread calling `Update` waits on the barrier, so no worker blocks. Runs on the regular 64 KB fiber stacks until something shows they are too small. The engine job system currently runs two workers (`JobSystem.cpp`), so physics gets two workers plus the waiting thread.
- [ ] **Shadow cascades and terrain follow one camera per scene.** `Scene::onUpdate` takes the camera from `m_activeController` and fits shadow views and terrain streaming to it (`Scene.cpp:188-211`). A second viewport on the same scene renders with cascades fitted to another camera. Cascades are per view; terrain streaming wants the union of views. Cascades fixed: `ViewCascadedShadows`, owned by each view's `DrawManager`, fits one map per cascaded light to the view's camera and hands them to its lighting through a per-view shadow buffer; the shadow pass picks terrain LOD from the view camera. Terrain streaming still follows the active controller's camera, waiting on the terrain sweep.
- [x] **DDGI and `RtInstanceData` per renderer.** Created in `DeferredRenderer`'s constructor (`DeferredRenderer.cpp:20-21`), so two views of one world trace two probe volumes. Per scene, like the TLAS already is. Fixed: the scene owns both (`Scene::enableGlobalIllumination`, called by a viewport that traces rays when it gets the scene), and the first renderer to draw the scene in a frame runs `Scene::updateGlobalIllumination`. Still open: the traced instance data (set 3, bindings 9 and 10) and the probe volume uniform are single global bindings, so two scenes with GI in the same frame overwrite each other. Passing them as bindless indices fixes that.
- [x] **Per-frame material sweep.** `Scene::onUpdate` walks every mesh's materials to poll pending textures (`Scene.cpp:171`), cost proportional to scene size every frame. Should be driven by texture load completion. Fixed: an instance that binds a texture still loading adds itself to `MaterialManager`'s waiting list, and `MaterialManager::bindLoadedPendingTextures` walks only that list once per frame from `Application::runFrame`. Materials outside static meshes now resolve too.
- [x] **Frames in flight equal the swapchain image count** (`Application.cpp:50`). A driver that returns four images gives four frames of latency and four copies of every per-frame resource. Fixed: `FRAMES_IN_FLIGHT = 2` for every application. The frame end records the graphics timeline value it submitted per slot, and the next frame on that slot waits for it (`Application::waitForFrameSlotToFinish`), so windowless runs and offscreen viewports are paced too. The swapchain keeps its own image ring for its own sync objects.
- [ ] **`Scene` reads the frame-in-flight index from `Application`.** The unused main swapchain fetch is gone (windowless step 4); `Scene::onUpdate` still takes the frame-in-flight index from `Application` for terrain and render data.

## Editor bindings on `Viewport`

`Viewport::EditorBinding` (`Viewport.h:66-70`) holds `controller`, `hovered` and `displayed`. They exist because the engine has no notion of "the camera this view is drawn from" beyond the scene's active controller, so the editor's per-viewport camera has to be registered on the scene and passed through the viewport.

- [ ] Shadow cascades and terrain take the camera of the view they are drawn for, not `Scene::activeController` (the item above).
- [ ] The scene's active controller stops feeding rendering; it only picks what play mode shows.
- [ ] `ViewportPanel` owns its editor camera and controller, the same panel that already owns the viewport's size, active state and hover. `hovered` and `displayed` go because ownership is structural, `controller` goes because the panel holds it, `EditorLayer`'s per-viewport map and the detach-by-name in play mode go with it, and `PreviewScene::focusOn` becomes a call on the panel.

The one-panel-per-viewport rule stays either way: the panel resizes the viewport to its pane, and two panes of different sizes would fight over it.

## Library layout

Layout: public headers in `Engine/include/rapture/<module>/`, private headers (anything only the engine includes) next to their sources in `Engine/src/<module>/` under their plain name. The `_internal` suffix is only for the private half of a header that also has a public one: a public `Texture.h` in `include/rapture/gpu/textures/` with its `_internal` companion in `src/gpu/textures/`. Every engine include is `<rapture/...>`, and the editor's include path only sees `include/`.

Measured 2026-11-15 by following includes from `Editor/src` and `Engine/test`: 170 of 245 headers are reachable, mostly through `app/Application.h` (which pulls in `VulkanContext`, `Scene`, `RenderWindow`, `ViewportManager`). The split starts from that set; shrinking it is its own step.

Vendor libraries by whether a reachable header includes them:

| Public | Private |
|---|---|
| Vulkan, VMA (`gpu/`), glm, spdlog (`Log.h`), Tracy (`TracyProfiler.h`) | GLFW, stb, glslang, toml++, Jolt, Lua, sol2, spirv_reflect, yyjson, concurrentqueue |

Amethyst is not included by any engine file; the editor links it itself.

- [x] **Step 1, CMake hygiene.** Target-scoped compile definitions, `RAPTURE_DEBUG` public so the editor and tests agree with the engine (`add_definitions` in `Engine/CMakeLists.txt` never reached them). The same warnings and `-Werror` in every configuration and on the editor, which had none. `RP_ASSERT` keeps its operands referenced in release so asserted-only variables do not warn. Vendor libraries split into a public and a private set, Amethyst linked by the editor. Dead `Engine/src/source.cpp` removed.
- [x] **Step 2, the split.** Reachable headers move to `include/rapture/`, the rest stay in `src/`, every engine include rewritten to `<rapture/...>`.
- [x] **Step 3, the cheap vendor leaks.** yyjson, concurrentqueue and spirv_reflect out of the public headers that include them, then linked private.
- [ ] **Next phase: the public surface.** `Application.h` stops pulling in the renderer and scene, so the reachable set shrinks to the actual API.
- [x] **Next phase: windowless.** `Application` without a window or swapchain, which the ECS and scene test fixtures need (`Scene`'s constructor builds physics and the TLAS). Steps below.

## Windowless

A windowless application has a Vulkan device and everything above it, but no GLFW, no window, no surface and no swapchain. Rendering goes to offscreen viewports, which already submit without the swapchain (`DrawManager::submitFrame`, `DrawManager.cpp:393`) and already pace themselves through the command pools' timeline waits (`CommandPool::resetIfNeeded`). What ties the engine to a window today:

| Where | What |
|---|---|
| `Application.cpp:36-52` | Creates the platform context, window, surface and swapchain; frames in flight come from the swapchain image count |
| `VulkanContext.cpp:76`, `:397` | Instance extensions come from the window |
| `VulkanContext.cpp:137`, `:525`, `:622` | Device choice requires a present-capable family and adequate swapchain support for the surface; `VK_KHR_swapchain` is a required device extension |
| `Application::run` | Polls the main window, closes on its close request |
| `DrawManager.cpp:28`, `:38` | Takes the main swapchain and its recreate signal for every viewport, offscreen ones included |
| `Scene.cpp:167` | Fetches the main swapchain and never uses it |

- [x] **Step 1, application config.** `Application` takes an `ApplicationConfig` (an optional main window), the editor passes its window. Windowless skips the platform context, window, surface and swapchain, and uses a fixed frames-in-flight count. `getMainWindow` and `createSecondaryWindow` assert they are not windowless.
- [x] **Step 2, device without a surface.** `VulkanContext` builds its instance from a given extension list instead of a window, and `initDevice` without a surface skips the present family, the swapchain support check and the swapchain extension. Present becomes optional (`QueueFamilyIndices::isComplete(presentRequired)`); the queue family fix itself moved to GPU synchronization, see the bug above.
- [x] **Step 3, the loop.** `run` becomes a loop over a public `runFrame`, so a test or a command interface can step frames itself. Windowless frames skip the main window.
- [x] **Step 4, window readers.** `DrawManager` takes the swapchain only for a swapchain target; the unused fetch in `Scene::onUpdate` goes.
- [x] **Step 5, fixture and smoke test.** A GoogleTest environment that brings up one windowless application for the engine test executable, then a test that builds a scene with a mesh, camera and light, renders an offscreen viewport for a few frames and reads the result back. The ECS and scene fixtures build on it. Lives in its own executable, `RaptureEngineGpuTests` (`Engine/test/gpu/`), so the CPU tests still run without a device. It writes the image it read back to `rapture_tests/windowless_render_sphere.png` in the temp directory.

Not in this phase: a windowless executable and the command socket (they come together), and running without a GPU at all.

Naming, from Thomas (2026-11-15): "headless" is meant as no Vulkan at all, for running the server side of a game. What this phase built is windowless rendering (a real device, offscreen only), so the proposed names are **windowless** for this mode, **headless** for a CPU-only runtime mode with no device, and **dedicated server** for a build that compiles rendering out. Agreed and renamed in code (`isWindowless`); "headless" is reserved for the CPU-only mode. Headless needs the CPU side of scenes and assets separated from their GPU mirrors; no software Vulkan.

## Testing and stress

GoogleTest is already wired (`Engine/test/event_signal_test.cpp`, `logging_test.cpp`, `file_watcher_test.cpp`, `Editor/test/text_buffer_test.cpp`).

- [ ] A test fixture that brings up the asset manager and the ECS without a window. Asset manager done (`Engine/test/asset_manager_fixture.h`), it takes frames in flight at init instead of reading `Application`; ECS still to do.
- [ ] Unit tests for each bug above as it is fixed. Unload double enqueue done (`Engine/test/asset_unload_test.cpp`).
- [ ] ECS: create/destroy churn, generation wrap, stale handles.
- [ ] Assets: load, release, reacquire within a frame; evict while loading; malformed and truncated asset files.
- [ ] Scene: spawn and destroy thousands of objects, destroy from inside a tick, reparent during iteration.
- [ ] Rendering: many viewports on one scene, resize storms, minimise and restore, opening and closing workspaces in a loop.
- [x] Run everything under Vulkan synchronization validation, which is what will surface the queue issues above. The GPU tests turn it on and fail on any validation error (`WindowlessGpuTest`); one renders with GI, so BLAS builds, the TLAS and DDGI run under it.

## Order

1. Unload null dereference, with the first fixture and test.
2. Library layout and the windowless fixture.
3. GPU synchronization: compute and transfer semaphores and ownership transfers, no `clear()` on acquire failure, frames in flight decoupled from the image count, the queue batch lock, command pools not keyed by thread.
4. Ownership: shared physics jobs, per-view cascades and terrain, per-scene DDGI, deferred destruction, entity generations.
5. Editor bindings off `Viewport`.
6. Fibers on Windows.
7. Scene object model and scripting review, with the stress tests above.

## Still open

- How terrain streaming chooses what to load when several views look at one scene.
- Whether frames in flight is fixed at two or configurable.

## Related

[[ECS Tiers and Ownership]], [[Editor Restructure]], [[Claude TODO]]
