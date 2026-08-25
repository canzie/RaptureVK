# Animation System

**Related: [[Skeleton]], [[Scene Object Model]], [[Scene Component]], [[Scene Object]], [[Curve Editor Widget]], [[Asset & Editor Roadmap]], [[RigidBody3D]], [[Transform Propagation]]**

The plan for skeletal animation: importing clips, playing them, blending them, correcting them against the world with IK, and driving all of it from a locomotion state machine. Design discussion 2026-08-24, after the skeleton workspace and its hierarchy panel landed.

The framing throughout is **viewing and integrating, not authoring**. Blender is a better animation tool than anything we would build, so keyframe authoring is explicitly out of scope. What belongs here is everything Blender cannot do: playing a clip against gameplay state, blending, runtime IK against real geometry, animation events, and a state machine.

---

## 1. Goals and non-goals

**Goals**

- Import glTF animations into a first-class clip asset.
- Play a clip against a [[Skeleton Pose]], scrub it in an editor timeline, and see it on the preview meshes.
- Animation events, so gameplay can hang footsteps, hitboxes and VFX off a clip.
- Root motion, exposed as a delta rather than applied, so the consumer decides what it means.
- Weighted blending of several clips, with per-joint masks.
- Runtime IK, primarily foot planting against real collision geometry.
- A locomotion state machine driven by a named parameter store.

**Non-goals**

- Keyframe authoring. Small tweaks eventually, full authoring never.
- Skeleton retargeting between differently-named rigs. Later, if ever.
- A visual animation graph editor. The state machine gets a data model first; whether it ever gets a node UI is a separate question.
- An equivalent of Unreal's montages. That concept exists to work around their state machine being hard to interrupt, and we should not inherit the workaround before we have the problem.

---

## 2. What already exists

Verified against the tree at time of writing.

- `Skeleton` holds joints as parallel arrays of parent index, name and rest transform, ordered so a joint always follows its parent (`Engine/src/assets/skeletons/Skeleton.h`). `Skeleton::JointTransform` is already the position/rotation/scale triple a clip samples into, and `findJoint` gives name lookup.
- `SkeletonPose` holds `std::vector<Skeleton::JointTransform> m_localPose` and pushes it to bones and to the GPU (`Engine/src/scene/instances/SkeletonPose.h:101`). **That vector is the output boundary of the entire animation system.** Nothing about it needs restructuring.
- `Bone3D` is one scene object per joint, created as an internal child of the pose, writing edits back through `SkeletonPose::setJointLocal` (`Engine/src/scene/instances/Bone3D.cpp:35`).
- `ASSET_ANIMATION` and its `"ANIM"` FourCC are already reserved (`Engine/src/assets/asset_manager/AssetCommon.h:36`, `:94`).
- `glTF2Loader::loadAnimation` is a `(void)animationVal;` stub (`Engine/src/assets/loaders/gltf/glTFLoader.cpp:1267`). The animations array is already fetched (`:325`) and `metadata.animationCount` already reported (`:958`).
- Jolt is integrated, with `PhysicsSystem::raycast` (`Engine/src/physics/PhysicsSystem.h:69`) and a `CharacterBody3D` exposing `velocity()`, `isOnGround()` and `groundState()`.
- `TickPhase` gives three ordered phases, and states the rule that registration order *within* a phase is arbitrary, so ordering is expressed by sitting in a later phase (`Engine/src/scene/TickPhase.h`).
- `SceneComponent` is described as "a capability that exists only as part of one scene object... holds no place in the scene tree, has no children and owns no entity of its own" (`Engine/src/scene/instances/SceneComponent.h`). `SceneObject::m_components` is an ordered vector exposed via `components()` (`Engine/src/scene/instances/SceneObject.h:257`).
- `ideas/Curve Editor Widget.md` already anticipates an animation view stacking transform channels.

Two pieces of debris to clear:

- `Engine/src/scene/components/AnimationComponent.h` is an empty stub containing a stray `FogType` enum. Delete it.
- `AnimationsWorkspace` already exists as an empty registered top-level workspace (`Editor/src/layers/workspaces/AnimationsWorkspace.h`). It will collide with the per-asset animation editor. Rename one before the collision happens.

---

## 3. Vocabulary

Written down because the terms get used loosely and half of them are engine-specific product names rather than concepts.

**Keyframe** — one time/value pair.

**Track** (or channel) — every keyframe for one joint and one property. Most skeletal animation only keys rotation, plus position on the root.

**Clip** — a bundle of tracks plus a duration. What an exported glTF animation is, and what `ASSET_ANIMATION` becomes.

**Pose** — a full set of local transforms, one per joint. `SkeletonPose::m_localPose` is a pose; the rest pose is one particular pose.

**Sampling** — evaluating a clip at a time. Find the two keyframes bracketing t per track, interpolate, write into a pose. The word carries no more meaning than that.

**Local space** — a joint relative to its parent joint. **Model space** — a joint relative to the character root. Clips store local, skinning needs model, and the conversion is the parent-chain multiply that `writeBoneMatrices` already does.

**Blending** — mixing several poses by weight. Only works in local space: blending model-space positions places a hand at the average of two world points and forces the arm to stretch, whereas blending local rotations preserves bone lengths. This is why the local/model split shows up everywhere in animation code.

**Masking** — blending applied to a subset of joints, as a per-joint weight. Upper body waves while lower body runs.

**Root motion** — a clip where the root joint actually travels rather than the character running in place. The travel is extracted from the pose and handed to whatever owns movement, so authored displacement and world displacement match exactly and the feet do not slide.

**Inverse kinematics** — solving for joint rotations given a desired end position, rather than the other way round. Runtime IK corrects a pose against a world the animator could not have known about.

**Blackboard** — a small store of named values (`speed`, `isGrounded`, `wantsJump`) that gameplay writes and the state machine reads. The entire interface between game logic and animation selection.

---

## 4. Reference: how the other engines are built

Kept because the proper nouns show up constantly in animation writing and are otherwise opaque.

### Unreal

**AnimSequence** is the clip asset, bound to a **Skeleton** asset that is separate from the mesh, so clips are shared across meshes. We already have this split.

An **Animation Blueprint** is the authored asset deciding what plays and how it mixes; its runtime instance is an **AnimInstance**, one per skeletal mesh component. It contains exactly two graphs:

- **EventGraph** — an ordinary visual-scripting graph on the game thread whose only job is to read gameplay state and stash it in variables. No pose math.
- **AnimGraph** — nodes are pose operations (play clip, blend, state machine, IK). Reads the cached variables, outputs a final pose, runs on a worker thread.

The split exists because Blueprint needed a threading story, not because the two jobs are conceptually different. **FAnimNode** is the C++ base for one AnimGraph node, with `Update` (advance time, compute weights) separate from `Evaluate` (produce bone transforms).

**Blend Space** maps a 1D or 2D input to a weighted mix of clips laid out on a grid — feed it speed and strafe direction, get back a mix of jog-forward and jog-left. The standard locomotion tool.

**AnimNotify** is an instantaneous event on a sequence; **AnimNotifyState** spans a time range with a begin and an end.

**Montage** wraps a clip for gameplay-triggered one-shots that must interrupt or layer over the state machine, with named sections. **IK Rig** defines bone chains and solvers; **IK Retargeter** maps animation between skeletons. **Fast Path** and **compact pose / bone container** are pure perf machinery, ignorable.

### Godot

**AnimationPlayer** plays one clip directly with crossfades, and Godot clips can key any node property, not just bones. **AnimationMixer** is the base class under it, owning libraries, resolving which node each track points at, and applying weighted results.

**AnimationTree** is the graph alternative; it plays nothing itself and takes a root **AnimationNode**. The subclasses are the vocabulary: `AnimationNodeAnimation`, `AnimationNodeBlend2`, `AnimationNodeBlendTree`, `AnimationNodeBlendSpace1D/2D`, `AnimationNodeStateMachine`.

**AnimationTree parameters** are a flat dictionary of every tunable in the graph, addressed by string path like `parameters/StateMachine/conditions/is_running`. Gameplay writes into it. This is Godot's blackboard; it is not a separate concept, just the tree's parameter dict.

**SkeletonModifier3D** (4.3+) is the piece worth stealing: anything that alters bones after the mixer has run, added as children of `Skeleton3D`, executing in child order. IK, jiggle physics and look-at all become modifiers rather than graph nodes. **SkeletonIK3D** is the older node, now deprecated and reparented under it.

Godot also deliberately does **not** apply root motion. It exposes `get_root_motion_position()` and makes the caller feed it to the body.

### Roblox

**Motor6D** is a joint connecting two parts, with a `Transform` property; classic Roblox characters are rigid parts so posing means writing that transform. **KeyframeSequence** is the clip, holding Keyframes each holding a tree of Poses. **Animation** is a stub asset holding an ID pointing at an uploaded KeyframeSequence.

**Animator** finds animatable joints under the model and blends every playing **AnimationTrack** each frame. **Priority** is an enum band (Core < Idle < Movement < Action): a higher band fully overrides lower ones for joints it touches, and within a band tracks mix by weight.

Two numbers and no graph at all, which covers most of what a game actually needs. Worth remembering before building a graph editor.

**Keyframe markers** are their notify equivalent, surfaced through `GetMarkerReachedSignal`.

### ozz-animation

A small open-source C++ library doing sample, blend and local-to-model and nothing else. No renderer, no scene graph, no state machine. Worth reading as the clearest published example of the data layout.

**Job** is its name for a stage: a plain struct with inputs, outputs and a `Run()`, so nothing owns state and everything threads trivially. **SoA** stores four joints' x components together so one SIMD instruction handles four joints. **Context** is per-clip, per-instance scratch remembering which keyframes you were between last frame, so playing forward is a pointer bump rather than a binary search — a genuinely useful trick independent of anything else here.

---

## 5. The design

```
AnimationClip (asset)     joint-indexed tracks, per-sampler interpolation, hasRootMotion
        |
AnimationPlayer           SceneComponent on the pose: time, sampling, looping,
        |                 events, root motion extraction
        |                 [later: blend layers, then a state machine over a blackboard]
        |
SkeletonPose              receives one whole local pose
        |
pose modifiers            SceneComponents on the pose, run in order, model space
        |
skinning matrices
```

### The clip asset

Tracks are **joint-indexed against a `Skeleton`**, not bound by node path. We already have stable joint indices and `findJoint` for name lookup, so binding is a name-to-index resolve once at load rather than a per-frame path walk. This is the ozz and Unreal model, and it is the one place Godot's design should not be copied.

glTF animates **nodes**, not joints, so import has to map each channel's target node to a joint index. The `jointOfNode` map built for skins at `Engine/src/assets/loaders/gltf/glTFLoader.cpp:1193` is exactly that mapping. Channels targeting non-joint nodes are object animation; drop them for now and record that we did.

glTF interpolation is `STEP`, `LINEAR` or `CUBICSPLINE`, stored per sampler, and cubic spline keys are in-tangent/value/out-tangent triples. Store the mode per track rather than assuming linear.

### The player is a component, not a node

`AnimationPlayer` is a [[Scene Component]] on the `SkeletonPose`. It does not select a pose — its target is `owner()`.

This is Unreal's arrangement (the AnimInstance belongs to the mesh component) rather than Godot's (a separate node pointing at a target by path). A player with no pose is meaningless and two players fighting over one pose is a bug, so there is nothing to gain from making the link configurable. `addComponent<T>` dedupes by type (`SceneObject.h:150`), which enforces one per pose for free.

It holds one active clip as an `AssetHandle` plus an `AssetRef`. The named clip set — Godot's AnimationLibrary, Unreal's Animation Blueprint — only becomes necessary when a state machine has to resolve "Walk" by name, so build it then. `play()` takes a handle rather than a pointer from the start so the ownership story does not change later.

### Pose modifiers are components too, not a third tier

IK solvers are **not** scene objects. [[Scene Object Model]] already decides this: *"if you would ever want two of them at different positions, it is a scene object"*. An IK solver has no position, so it is a component.

They are also not a new concept alongside scene objects and scene components. `SceneComponent`'s own description already reads as a definition of a pose modifier. And the one-per-type restriction is not needed: `attachComponent` does not dedupe, and `m_components` is an ordered vector, so two feet, two hands and a look-at are five components of two types in a defined order. That is Godot's child-order guarantee using machinery that already exists.

If a second system ever wants ordered per-object passes, generalise then, not now.

### Ordering, forced by the tick rule

`TickPhase.h` states that registration order within a phase is arbitrary and ordering must be expressed by sitting in a later phase. So modifiers must **not** self-tick and rely on component order — the pose runs them itself, in `components()` order, from a single tick.

The three existing phases line up with the three stages:

| Phase | Who | What |
| --- | --- | --- |
| `TICK_INPUT` | AnimationPlayer | sample the clip, write the pose, extract root motion |
| `TICK_PRE_PHYSICS` | the character class | read the root motion delta, move the body |
| `TICK_POST_PHYSICS` | SkeletonPose | run pose modifiers |

Foot IK raycasting in `TICK_POST_PHYSICS` is not incidental — it is the only point in the frame where the body is actually where it ended up, so the ground under the foot is the real ground.

The stretch is that sampling animation is not "input". Either rename that phase or add one; see open questions.

### Root motion is exposed, never applied

Extraction, inside the player:

1. Sample at the new time; the root joint's transform includes the travel.
2. Delta is root-at-`newTime` minus root-at-`prevTime`. If the clip looped this frame that is `(end - prev) + (new - start)`, not `new - prev`.
3. Strip it from the pose by pinning the root joint back to its rest transform.
4. Accumulate it for whoever asks.
5. Push the de-rooted pose.

The consumer is **user code**, and that is the whole point of exposing rather than applying. Whether the delta becomes a velocity, a teleport, or is ignored depends entirely on what is moving. In the animation preview workspace nobody consumes it and the character animates in place, which is what an asset editor should do — if the pose applied root motion itself, the preview would walk out of frame.

Today user code means a C++ scene object class, the same tier the existing controllers live at. That is already user scripting; the point is not that no scripting is involved, it is that **no new language runtime is a prerequisite**. If Lua lands later it consumes the delta through the same accessor.

When blending, each layer contributes a weighted delta, so root motion blends alongside the pose rather than being read off the winner.

### IK: one solver, not a feature

Build `TwoBoneIk`, parameterised by end joint, chain length, target position and a pole hint. That is Blender's IK constraint minus the authoring UI, and for two bones it is closed form — bone lengths and the hip-to-target distance give the knee angle from the law of cosines, and the pole hint resolves the remaining rotation about the hip-to-ankle axis. No iteration.

For the record, since it is a recurring confusion: Blender's IK is a **bone constraint**, applied in Pose Mode to the **last bone of the chain** with a `Chain Length` counting upward, targeting a control bone plus an optional pole target. Blender's **modifier** stack is a different thing living on the mesh object, and the Armature modifier there is what binds mesh to armature. Blender's constraint, not Blender's modifier, is the analogue of what we are calling a pose modifier.

Authoring IK never reaches us regardless. Blender bakes it to plain FK rotations on export, and glTF has no representation for a constraint, so by the time a clip arrives the rig is gone.

Foot planting is then the solver plus a target provider:

1. Pose is computed, so model space is available for every joint.
2. Raycast down from each ankle; take hit point and normal.
3. Drop the pelvis by the largest downward correction of the two legs **first**, or on a steep slope the far leg hyperextends reaching for the ground. This is the step everyone forgets.
4. Run `TwoBoneIk` on hip/knee/ankle to place each ankle.
5. Rotate the foot to the ground normal, clamped.

FABRIK and CCD are iterative solvers for longer chains — spine, tail, tentacles. Legs and arms are two bones, so they are not needed there.

A modifier reads model space from `m_boneMatrices`, solves, and writes back through `setJointLocal`, which folds the result into `m_localPose` and recomputes the chain. That round-trips correctly today with no new plumbing.

### The state machine reads a blackboard

States play a clip or a blend; transitions carry conditions over named parameters; entering a state crossfades from the previous pose.

Conditions realistically need `param > x`, `param < x`, a bool, and fire-once triggers that clear when consumed. That is a small struct, not an expression language — which is why the state machine does not depend on a scripting runtime existing. Gameplay writes the parameters, from C++ today and from Lua later, through the same interface.

---

## 6. Decisions

Until something proves them wrong.

1. **Clips are joint-indexed against a Skeleton**, resolved by name once at import. Never node-path bound.
2. **The player is a SceneComponent on the SkeletonPose**, one per pose, targeting its owner.
3. **Pose modifiers are SceneComponents on the SkeletonPose**, several allowed, ordered by the component vector.
4. **No third tier.** Nothing new alongside scene objects and scene components.
5. **The pose drives its modifiers** from one tick rather than letting them self-register, because within-phase order is arbitrary by design.
6. **Root motion is exposed as a consumable delta**, never applied by the pose.
7. **`hasRootMotion` and the root joint index go on the clip from day one**, so no animation asset needs re-serialising later.
8. **One generic `TwoBoneIk` solver plus target providers**, not a monolithic foot-IK feature.
9. **The blackboard is language-agnostic** and does not wait on a scripting runtime.

---

## 7. Still open

- **Tick phase naming.** Sampling animation in `TICK_INPUT` is a semantic stretch. Rename the phase, add a fourth, or accept it.
- **Scripting order.** Whether Lua lands before the state machine. Nothing in sections 1 to 5 changes either way, which is the argument for deferring the decision rather than blocking on it. Lua vs AngelScript, native bindings and hot reload want their own doc.
- **`AnimationsWorkspace` name collision** with the per-asset animation editor.
- **Two parallel derivations of model space.** `writeBoneMatrices` computes model space from `m_localPose` directly (`SkeletonPose.cpp:128`) and never reads `Bone3D`'s world transforms, so the bone tree and `m_boneMatrices` derive the same thing twice. Fine today; worth watching once modifiers start writing into the middle of it.
- **Object animation.** glTF channels targeting non-joint nodes are dropped at import. Whether animated non-skeletal objects get their own path is unanswered.
- **Whether the state machine ever gets a node UI**, or stays a data model configured in the details panel.

---

## 8. Order of work

1. **Clip asset and glTF import.** Fill the `loadAnimation` stub. Joint-indexed tracks, per-sampler interpolation, duration, root motion flag. Visible in the content browser.
2. **Player: sample one clip, push the pose.** First point where something moves.
3. **Animation workspace and timeline panel.** Scrub, play/pause/loop, clip dropdown. Reuses `AssetPreviewWorkspace` wholesale, and the skeleton hierarchy panel drops in unchanged.
4. **Animation events**, once there is a timeline lane to put them on. Fire by sweeping `[prevTime, newTime]` rather than testing the instant, so scrubbing, looping and playback rate all behave.
5. **Root motion extraction** plus a character class consuming it.
6. **Blending** — N weighted layers plus per-joint masks.
7. **Foot IK** as a pose modifier.
8. **Blackboard and state machine.**

### One API change needed at step 2

`setJointLocal` calls `writeBoneMatrices()` on every call (`SkeletonPose.cpp:121`), and `writeBoneMatrices` rebuilds the entire model-space chain from `m_localPose`. That is fine for a gizmo dragging one bone, but a player writing sixty joints a frame would rebuild the chain sixty times.

It needs a bulk `setPose(std::span<const Skeleton::JointTransform>)` that pushes every bone and writes the matrices once.
