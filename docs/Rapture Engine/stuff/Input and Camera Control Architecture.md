# Input and Camera Control Architecture

## Purpose

Defines how **input**, **controllers**, **cameras**, **pawns** and **viewports** relate in Rapture — for both the editor (free-fly/orbit navigation) and the eventual game runtime (players, possession, third-person). The guiding rule: use ECS where it is elegant (world data: transforms, meshes, hierarchy, renderable cameras), use plain objects where the concept is *logic or identity* (controllers, input, players). See [[Editor Layout Design]] and [[SceneRenderData]].

## The layers (each does exactly one job)

| Layer | What it is | Its one job | Owner |
|-------|------------|-------------|-------|
| **Camera** | entity: `Transform` + `CameraComponent` | be a viewpoint (an eye) | editor: the **editor camera rig** (its own, transient); game: the **Pawn** (child entity) |
| **Pawn** | entity: `Transform` + mesh/etc (optional) | be the body that moves | the **Scene** |
| **Controller** | plain object | read an `Input` → move a transform (fly / orbit / walk) | editor: the **editor camera rig**; game: the **Player** |
| **Input** | plain object | expose device/AI state (poll + per-frame delta/scroll) | editor: one, the focus router; game: the **Player** |
| **Viewport** | engine object | **render a Scene from a Camera into an image** | editor; game screen |

### The key decoupling

> A **Viewport** only knows "render *this scene* from *this camera*." It references a camera (`m_camera`, an `Entity`); it owns **no controller** and does **not** know who moves the camera. The **Controller** is upstream and invisible to the viewport — it just mutates a transform each frame.

This is why a shipped game has no "dead" controller hanging off the viewport: the viewport never owns one. The controller is owned by whatever drives the camera — the **editor camera rig** in the editor, or the **Player** in a game.

This is why everything else composes: who *moves* the camera (player input / AI / editor fly-cam / a cutscene / nothing) is independent of who *renders* it.

## Camera lookup: Scene → Viewport

Today camera resolution is scene-centric (`Scene::getMainCamera()` scans for `CameraComponent.isMainCamera`). `Viewport::drawFrame` already prefers its own camera with a scene fallback:

```cpp
Entity camera = m_camera.isValid() ? m_camera : m_scene->getMainCamera();
```

New rule: **the Viewport is the source of truth for "what eye am I rendering from."**

- **Editor viewport**: creates and owns its **own editor camera**, so `m_camera` is *always* valid. It never falls back to a scene camera while authoring.
- **Game viewport** (play mode): its camera is set to the active player's camera entity.
- `Scene::getMainCamera()` is retained only as the **game default-camera** convenience (which camera plays if a level defines no explicit player camera), not as the editor's view source.

### Fallback / "no camera" question

There is never a black screen in the editor: the **viewport guarantees its own editor camera** at construction, independent of scene contents. The scene having zero cameras is fine — the editor still renders through its viewport camera. For a *game* viewport with no camera, the fallback is the scene default camera; only if that is also absent do we show an explicit "No Camera" state (never silent black).

### Authoring a player/camera changes nothing until Play

Adding a `Player` + camera + controller to the scene changes the editor view **zero** — the editor viewport keeps rendering through its own editor camera (the scene camera is just data, optionally drawn as a frustum gizmo). Only on **Play** does a game viewport render through the player's camera. Edit view = editor camera; play view = game camera.

## Editor slice (what we build first)

```
EditorInput (KBM)  ── one, owned by the editor ── ticked once/frame
        │  routed by focus
        ▼
Viewport (focused)
   owns ─ Camera (editor camera entity: Transform + CameraComponent)
   owns ─ Controller (fly / orbit) ── reads EditorInput ── drives the camera
   renders Scene from its Camera ──▶ image
```

- **One `Input`** for the editor's keyboard+mouse. Not a `PlayerInput` (there is no player in the editor) — just the base device input, owned by the editor / a small viewport-focus router.
- **Each Viewport owns its camera + its controller.** Per-viewport state (yaw/pitch/orbit-focus) is exactly what lets two viewports of one scene move independently with the same mouse.
- **Focus routing**: the editor feeds the one live `Input` to the **focused** viewport's controller each frame (driven by which `ViewportPanel` is hovered/clicked); unfocused viewports' cameras hold their pose. This is an **editor-only** concern.

### Where the editor controller lives

On the **`Viewport`** (it owns camera + controller, 1:1 with the view). The editor *drives* it (the hotbar toggle calls `viewport->cameraController().setMode(...)`, focus changes pick which controller gets input) but does not *own* it as a loose member. This keeps multi-viewport uniform.

## Input class shape

There are **two** layers, and they must not be confused:

**Device layer** (knows about hardware):
- **`Input`** (base, instantiable, no statics): polls a `WindowContext` for keys / mouse buttons / cursor; computes per-frame mouse delta; latches scroll (the one event-driven bit, via the `onMouseScrolled` bus); owns cursor capture (`setCursorMode`, swallowing the warp delta). Subclassable later for gamepad.
- **`PlayerInput : Input`** (game-facing sugar): action/rebind/gamepad-assignment growth path. Owned by a `Player`.

**Intent layer** (device-agnostic — what controllers actually consume):
- **`ControlInput`** — a plain per-frame struct: `look` (yaw/pitch delta), `move` (-1..1 per axis), `zoom`, plus action flags (`orbit`, `pan`, `releaseControl`, later `jump` etc.). This is the only thing a controller sees.
- **A mapping step** turns a device `Input` into a `ControlInput` (`mapEditorCameraInput`, `mapPlayerInput`, ...). This single function is where keyboard-vs-gamepad lives and where user-defined remapping would later plug in — UE's "Input Mapping Context / Input Action."

So a controller **does not own an `Input`** and never learns whether "forward" was `W` or a stick — it is *handed* a `ControlInput` each frame. The device `Input` is owned upstream (editor focus router / `Player`); the mapping converts device → intent; cursor capture is a device side-effect the **owner** applies based on what the controller reports it wants (`desiresCursorCapture()`).

Cursor capture uses `CursorMode { NORMAL, HIDDEN, DISABLED }` on `WindowContext`. `DISABLED` (GLFW `GLFW_CURSOR_DISABLED` + raw motion) gives unbounded relative motion locked to the window — the actual fix for "mouse leaves the viewport while flying."

## Controllers in detail

A controller is **a plain class** (the "brain"): holds an `Input*` (driver — may be a device, AI logic, or nothing), a handle to the **possessed entity** (the transform it moves), its control state, and the per-frame update logic. Possession is an explicit reference you reassign — not a flag, and orthogonal to whether an input is attached (an idle pawn = possessed, no input; an AI pawn = possessed, AI driver; a player pawn = possessed, device input).

- **Editor fly/orbit controller** possesses the **camera entity directly** (no pawn). Fly = WASD + mouse-look with cursor captured; Orbit = MMB-orbit / Shift+MMB-pan / scroll-zoom around a focus point.
- **Game controllers** (later) possess a **pawn** and drive a child camera; the camera's follow/offset (first vs third person) lives in the camera's own component reading the parent transform via [[HierarchyComponent]].

End users only meet controllers in gameplay scripting (possess / swap input). The editor camera controller is invisible engine plumbing.

## Third-person note (camera transform)

Camera and pawn each have their own `Transform`. A third-person camera follows the pawn's **position** but keeps its **own orientation** — it is *not* a rotation-child of the pawn, otherwise turning the character would drag the view. A spring arm pivots at `pawn.position + pivotOffset`, orients by its own boom yaw/pitch, and places the camera `length` back (with optional collision pullback — UE's spring-arm idea). First person = near-zero arm. Editor fly-cam = no target, transform written directly. The camera always has its own transform; what differs is how it is *derived*.

Crucially, `look` and `move` are consumed by **different systems** and never coupled: the pawn controller reads `move` (locomotion), the camera rig reads `look`/`zoom` (orbit). That separation is *why* WASD movement and mouse orbit are independent (orbit around to see the character's front/back while walking).

## Customising camera logic (behaviors + scripting)

A fixed spring arm is preprogrammed and limiting. Instead, a camera's final pose is produced by a **composable stack of behaviors** (cf. Unity Cinemachine's Body/Aim/Noise/Extensions, UE's `CameraModifier` stack):

- A camera entity holds an ordered `CameraRigComponent { Entity target; vector<ICameraBehavior> stack; }`.
- Each frame a base pose is seeded from the target and threaded through the stack: `pose = behavior.evaluate(ctx)`, where `ctx` exposes `dt`, `time`, `self`, `target`, the live `ControlInput`, and the `current` pose.
- The **spring arm is just the first (replaceable) behavior**; follow/fixed/rail/top-down are alternative base behaviors. Sway / shake / lag / recoil are **modifier** behaviors stacked on top.

```cpp
struct CameraPose { glm::vec3 position; glm::quat rotation; float fov; };
struct CameraContext { float dt, time; Entity self, target; const ControlInput *input; CameraPose current; };
class ICameraBehavior { public: virtual CameraPose evaluate(const CameraContext &) = 0; };
```

**Where a game dev customises:** either pick + tune a built-in behavior (data params), or add a `ScriptCameraBehavior` wrapping a script that implements `evaluate(ctx) -> pose`. The engine calls it each frame and uses the returned pose; new camera feels (head-bob, dolly-zoom, custom follow) require zero engine changes. This assumes a scripting binding exists; a native behavior and a scripted one are interchangeable implementers of `ICameraBehavior`.

### Tools, not config

Predefined options can never cover the combinatorial space, so configuration is treated as **a cache of common compositions, not the ceiling**. The engine ships **primitives + helpers** that scripts compose, in three tiers built on the same functions:

1. **Primitives** the script can call: `slerp`/`lerp`/`damp(current, target, lambda, dt)`, spring-damper, `raycast` (e.g. for collision pullback — a *script* decision, not a baked feature), `transformOf(entity)`, `lookAtQuat(...)`, target queries.
2. **Helpers** (curated compositions, still just functions): `springArm(ctx, length)`, `lookAt(pose, targetPos)`, `lockOn(pose, target, lambda)`. A Dark Souls-style lock-on is the script calling `lookAt`+`slerp`+`damp` when gameplay sets a target — the helper does the math, the *policy* (when to lock, what to frame, blend speed) stays in user code.
3. **Built-in behaviors / sliders** are those same helpers wrapped as `ICameraBehavior` for the no-code path. There is no cliff between config and code: when params run out, drop to a `ScriptCameraBehavior`. (This mirrors Cinemachine: components for ~90%, custom extensions for the rest.)

Rule of thumb: **helpers remove boilerplate; they never make decisions.** Decisions live in user code.

This whole behavior/scripting system is **game-runtime and deferred** — the editor fly-cam uses the simple direct `CameraController`, not a rig.

## Build order

1. `CursorMode` on `WindowContext` + `GlfwWindowContext` (done: enum added).
2. `Input` base (poll + delta + scroll + cursor capture); `KeyCode`/`MouseButton` enums; drop `Keybinds.h`.
3. Editor owns one `Input`; tick once/frame.
4. `Viewport` owns an editor camera + a fly/orbit controller; render through it.
5. Editor focus router feeds the live `Input` to the focused viewport's controller.
6. Viewport hotbar toggle for Fly ↔ Orbit.

Deferred: `PlayerInput` sugar, gamepad `Input` subclass, the full Player→Controller→Pawn→Character tower + GameMode-style spawning/possession, multi-viewport editing beyond one viewport.

## Note: single-viewport assumption (revisit with multi-viewport)

`EditorLayer` and `ViewportPanel` currently hardcode `ViewportManager::getPrimaryViewport()` — there's one editor camera + controller, registered on the primary viewport, and `EditorBinding.hovered` is pushed/read on it. The intended multi-viewport model: creating a viewport spawns its own camera + controller, and the hovered viewport becomes the active input target (the focus router decides which viewport's controller the one editor `Input` drives). The `EditorBinding` struct on the viewport and the `hovered` flag are the seeds of that; the router is the missing piece. Until then, primary-viewport-only is the simplification.
