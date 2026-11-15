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
| Testability | 3 | GoogleTest is already in place (`Engine/test/`, `Editor/test/`), but `Application`'s constructor creates the window and device, so little above `core/` can be built in a test. Same work as the headless run. |
| Library hygiene | 4 | Include root is `src/` so every header is public; vendor libraries link `PUBLIC`; `add_definitions` is global; `-Werror` only in Debug (`Engine/CMakeLists.txt`). |

Statics for `Application`, `AssetManager` and the job system are intentional and not counted against anything.

## Bugs

Ordered by how likely they are to be hit.

- [x] **Asset unload null dereference.** `AssetManagerEditor::processUnloadRequests` null-checks `slot->asset` before the status check but then calls `slot->asset->useCount()` unconditionally (`AssetManagerEditor.cpp:795`). Every last release enqueues the handle (`Asset.cpp:23`), so an asset released to zero twice in one frame is dequeued twice: the first evicts it (non-virtual assets keep their slot with a null payload), the second dereferences null. `EVICT_IMMEDIATE` is the default policy. First unit test to write.
- [ ] **Fibers crash on Windows.** The context switch is SysV x86-64 assembly (`Fiber.cpp:38`): arguments in rdi/rsi and only the SysV callee-saved registers. Win64 passes in rcx/rdx, also preserves rdi, rsi and xmm6-15, and needs the TIB stack base/limit swapped, or stack probes and SEH fail. Use `CreateFiber`/`SwitchToFiber` on Windows, or Win64-correct assembly.
- [ ] **DDGI races the lighting pass.** The probe update is submitted on the compute queue with no semaphores (`DynamicDiffuseGI.cpp:353`) while the graphics queue samples the same images. Queue families are spread across distinct families by `findQueueFamilies`, so nothing orders the two.
- [ ] **No queue family ownership transfers.** Every buffer and image is `VK_SHARING_MODE_EXCLUSIVE` and no barrier anywhere sets a real `srcQueueFamilyIndex`, yet uploads run on the transfer family and DDGI on the compute family. Works on NVIDIA, undefined by the spec.
- [ ] **A failed acquire discards batched work.** `RenderWindow::beginFrame` calls `getGraphicsQueue()->clear()` when acquire fails (`RenderWindow.cpp:81`), which drops everything batched so far: offscreen viewport frames (`DrawManager.cpp:396`), BLAS builds (`AccelerationStructureBuilder.cpp:259`, `:313`) and IBL bakes (`ImageBasedLighting.cpp:271`). A dropped BLAS build leaves the TLAS referencing an unbuilt BLAS. Hit on window resize.
- [ ] **Destroying during a tick frees a running object.** `Scene::destroyInstance` removes immediately (`Scene.cpp:333`) while `runTickPhase` iterates the live list (`Scene.cpp:316`). Not reachable yet because Lua cannot destroy; it will be the moment it can. Destruction needs deferring to the end of the phase.
- [ ] **Entity generation wraps.** 12 generation bits (`core/ecs/common.h:18`) with LIFO slot reuse: one spawn and destroy per frame wraps a slot in about 68 s at 60 fps, after which a stale `Entity` validates.
- [ ] **Queue batch read without its lock.** `VulkanQueue::submitAndFlushQueue` (`VulkanQueue.cpp:205`, already a TODO).
- [ ] **Command pools keyed by thread id under fibers.** `DynamicDiffuseGI.cpp:294`, `SkyboxPass.cpp:70`, `Texture.cpp:66`, `:329`, `:1690`. Two jobs interleaving on one worker share a pool whose bookkeeping is non-atomic.
- [ ] **Component type ids across modules.** `NextComponentTypeId` is a non-atomic static in an inline function (`component_pool.h:22`). Games never define components, but a C++ scripting module that instantiates engine ECS templates inline (any scene object accessor that reaches `registry.read<T>`) gets its own counter and disagrees with the engine on ids. Matters only once C++ scripting exists.
- [ ] **Latent: layer and window lists mutated while iterated.** `Application::run` iterates `m_layerStack` and `m_secondaryWindows` with iterators (`Application.cpp:198-230`); `pushLayer` inserts into the same vector. Clicks are dispatched from `glfwPollEvents`, outside those loops, so nothing hits it today. Shortcuts run inside `AmethystLayer::onUpdate`, inside the loop, so the first shortcut that pushes a layer will.

## Ownership and scale

What is owned at the wrong level. The per-asset / per-scene / per-view rule is already decided in [[ECS Tiers and Ownership]]; these are the places that do not follow it yet.

- [ ] **Physics thread pool per scene.** Every `Scene` builds a `PhysicsSystem` (`Scene.cpp:56`) and each initialises a Jolt `JobSystemThreadPool` with `hardware_concurrency - 1` threads (`PhysicsSystem.cpp:127`), preview scenes included. One shared physics job system, or Jolt running on the engine's job system.
- [ ] **Shadow cascades and terrain follow one camera per scene.** `Scene::onUpdate` takes the camera from `m_activeController` and fits shadow views and terrain streaming to it (`Scene.cpp:188-211`). A second viewport on the same scene renders with cascades fitted to another camera. Cascades are per view; terrain streaming wants the union of views.
- [ ] **DDGI and `RtInstanceData` per renderer.** Created in `DeferredRenderer`'s constructor (`DeferredRenderer.cpp:20-21`), so two views of one world trace two probe volumes. Per scene, like the TLAS already is.
- [ ] **Per-frame material sweep.** `Scene::onUpdate` walks every mesh's materials to poll pending textures (`Scene.cpp:171`), cost proportional to scene size every frame. Should be driven by texture load completion.
- [ ] **Frames in flight equal the swapchain image count** (`Application.cpp:50`). A driver that returns four images gives four frames of latency and four copies of every per-frame resource.
- [ ] **`Scene` reads the main window.** `Scene::onUpdate` fetches the main swapchain (`Scene.cpp:167`), unused, and the frame-in-flight index from `Application`.

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
- [ ] **Next phase: headless.** `Application` without a window or swapchain, which the ECS and scene test fixtures need (`Scene`'s constructor builds physics and the TLAS).

## Testing and stress

GoogleTest is already wired (`Engine/test/event_signal_test.cpp`, `logging_test.cpp`, `file_watcher_test.cpp`, `Editor/test/text_buffer_test.cpp`).

- [ ] A test fixture that brings up the asset manager and the ECS without a window. Asset manager done (`Engine/test/asset_manager_fixture.h`), it takes frames in flight at init instead of reading `Application`; ECS still to do.
- [ ] Unit tests for each bug above as it is fixed. Unload double enqueue done (`Engine/test/asset_unload_test.cpp`).
- [ ] ECS: create/destroy churn, generation wrap, stale handles.
- [ ] Assets: load, release, reacquire within a frame; evict while loading; malformed and truncated asset files.
- [ ] Scene: spawn and destroy thousands of objects, destroy from inside a tick, reparent during iteration.
- [ ] Rendering: many viewports on one scene, resize storms, minimise and restore, opening and closing workspaces in a loop.
- [ ] Run everything under Vulkan synchronization validation, which is what will surface the queue issues above.

## Order

1. Unload null dereference, with the first fixture and test.
2. Library layout and the headless fixture.
3. GPU synchronization: compute and transfer semaphores and ownership transfers, no `clear()` on acquire failure, frames in flight decoupled from the image count, the queue batch lock, command pools not keyed by thread.
4. Ownership: shared physics jobs, per-view cascades and terrain, per-scene DDGI, deferred destruction, entity generations.
5. Editor bindings off `Viewport`.
6. Fibers on Windows.
7. Scene object model and scripting review, with the stress tests above.

## Still open

- Whether Jolt runs on the engine's fiber job system or keeps one shared thread pool of its own.
- How terrain streaming chooses what to load when several views look at one scene.
- Whether frames in flight is fixed at two or configurable.
- Whether entity handles widen to 64 bits or keep 32 with more generation bits and fewer index bits.

## Related

[[ECS Tiers and Ownership]], [[Editor Restructure]], [[Claude TODO]]
