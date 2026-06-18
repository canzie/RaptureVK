# Editor Component Fields

A per-component breakdown of editable fields for the Properties/Inspector panel. Each entry lists the **widget type**, whether it's **needed** (required for proper functionality) or **nice** (quality-of-life/polish), and notes. Widget vocabulary: `input` (numeric/text entry), `drag` (drag-scrub float), `slider` (bounded range), `dropdown` (enum), `checkbox` (bool), `color picker`, `vec3 drag` (XYZ drag row), `asset picker` (drag-drop / browse target), `button`, `curve/graph editor`, `gradient`.

Components live in `Engine/src/components/`. Terrain is intentionally excluded (needs its own panel).

---

## TagComponent
| Field | Widget       | Priority | Notes                                                   |
| ----- | ------------ | -------- | ------------------------------------------------------- |
| `tag` | input (text) | needed   | Entity name shown in outliner. Should be live-editable. |

---

## TransformComponent
Backed by `Transforms` (decomposed T/R/S kept in sync with the matrix).

| Field                | Widget            | Priority | Notes                                                                   |
| -------------------- | ----------------- | -------- | ----------------------------------------------------------------------- |
| translation          | vec3 drag         | needed   | XYZ, small step (~0.01–0.1).                                            |
| rotation             | vec3 drag         | needed   | Euler degrees. Consider a quaternion/gimbal-safe mode later.            |
| scale                | vec3 drag         | needed   | Add a **uniform-scale lock** toggle (chain icon) so XYZ scale together. |
| reset transform      | button            | nice     | Snap back to identity.                                                  |
| local vs world space | dropdown / toggle | nice     | Display values in world space when entity is parented.                  |
| copy/paste transform | button            | nice     | Common DCC convenience.                                                 |

---

## CameraComponent
| Field           | Widget           | Priority | Notes                                                                 |
| --------------- | ---------------- | -------- | --------------------------------------------------------------------- |
| `fov`           | slider           | needed   | Range ~10–120°.                                                       |
| `nearPlane`     | drag             | needed   | Min clamp > 0.                                                        |
| `farPlane`      | drag             | needed   | Must stay > near.                                                     |
| `aspectRatio`   | input + checkbox | nice     | "Auto from viewport" checkbox; manual input when unchecked.           |
| `isMainCamera`  | checkbox         | needed   | Radio-like: setting one should unset others.                          |
| projection type | dropdown         | nice     | Perspective/Orthographic (currently perspective only — creative add). |


---

## CameraControllerComponent
Holds a `CameraController`. Expose tunables (speed, sensitivity, smoothing) from the controller:

| Field             | Widget   | Priority | Notes                                       |
| ----------------- | -------- | -------- | ------------------------------------------- |
| move speed        | drag     | needed   |                                             |
| look sensitivity  | slider   | needed   |                                             |
| controller mode   | dropdown | nice     | Fly / Orbit / FPS, if multiple modes exist. |
| smoothing/damping | slider   | nice     |                                             |
| invert Y          | checkbox | nice     |                                             |

---

## MeshComponent
| Field                | Widget       | Priority | Notes                                                                     |
| -------------------- | ------------ | -------- | ------------------------------------------------------------------------- |
| `mesh` (AssetRef)    | asset picker | needed   | Drag-drop from content browser; show mesh name + vert/tri count.          |
| `mobility`           | dropdown     | needed   | `MOBILITY_STATIC` / `MOBILITY_DYNAMIC` — affects batching/shadow caching. |
| `isEnabled`          | checkbox     | needed   | Toggle visibility/render.                                                 |
| `isLoading`          | status label | nice     | Read-only spinner/badge while async-loading.                              |
| cast/receive shadows | checkbox     | nice     | If exposed per-mesh later.                                                |
| LOD bias             | slider       | nice     | Creative: per-instance LOD override.                                      |

---

## MaterialComponent / MaterialInstance
This is the big one. A compact inline editor in the Properties panel, plus a **"Open Material Editor" button** that launches a dedicated node-graph workspace (Blender-shader-style) for advanced authoring.

### Inline editor (driven by `PARAM_REGISTRY`)
| Field                  | Widget             | Priority | Notes                                                                |
| ---------------------- | ------------------ | -------- | -------------------------------------------------------------------- |
| material asset         | asset picker       | needed   | Swap which `MaterialInstance` is bound.                              |
| albedo (rgb)           | color picker       | needed   | `ParameterID::ALBEDO`.                                               |
| alpha                  | slider             | needed   | `ALPHA`, 0–1.                                                        |
| roughness              | slider             | needed   | 0–1.                                                                 |
| metallic               | slider             | needed   | 0–1.                                                                 |
| ao                     | slider             | needed   | 0–1.                                                                 |
| emissive color         | color picker (HDR) | needed   | `EMISSIVE` rgb.                                                      |
| emissive strength      | drag               | needed   | emissive.a, unbounded ≥ 0.                                           |
| albedo map             | texture picker     | needed   | Drag-drop; thumbnail + clear button. Sets `MAT_FLAG_HAS_ALBEDO_MAP`. |
| normal map             | texture picker     | needed   |                                                                      |
| metallic-roughness map | texture picker     | needed   |                                                                      |
| ao map                 | texture picker     | needed   |                                                                      |
| emissive map           | texture picker     | needed   |                                                                      |
| height map             | texture picker     | nice     | Parallax/displacement.                                               |
| specular map           | texture picker     | nice     |                                                                      |
| tiling scale           | drag               | nice     | `TILING_SCALE`.                                                      |
| height blend           | slider             | nice     | `HEIGHT_BLEND`, 0–1.                                                 |
| slope threshold        | slider             | nice     | `SLOPE_THRESHOLD`, terrain-ish.                                      |
| material flags         | checkbox group     | nice     | Read-only mirror of auto-set flags; useful for debugging.            |

### Advanced Material Editor (new workspace/panel)
- Node graph (texture samplers, math nodes, mix, fresnel, normal blend, etc.) → **curve/graph editor** canvas.
- Live material preview sphere/cube with rotatable lighting.
- Save/load material presets.
- This is a "nice → eventually needed" feature once the PBR set outgrows flat sliders.

---

## LightComponent
Has a generation counter; all writes should go through the `setXxx()` setters so the renderer re-uploads.

| Field                | Widget           | Priority            | Notes                                                      |
| -------------------- | ---------------- | ------------------- | ---------------------------------------------------------- |
| `type`               | dropdown         | needed              | Point / Directional / Spot — **drives which fields show**. |
| `color`              | color picker     | needed              | Use `setColor()`.                                          |
| `intensity`          | drag             | needed              | Unbounded ≥ 0; consider a slider for typical range.        |
| `range`              | drag             | needed (point/spot) | Hide for Directional.                                      |
| `innerConeAngle`     | slider (degrees) | needed (spot)       | Store radians, edit degrees. Clamp inner ≤ outer.          |
| `outerConeAngle`     | slider (degrees) | needed (spot)       |                                                            |
| `isActive`           | checkbox         | needed              |                                                            |
| `castsShadow`        | checkbox         | needed              | Toggling should add/remove the matching shadow component.  |
| `mobility`           | dropdown         | needed              | Static/Dynamic — shadow caching.                           |
| temperature (Kelvin) | slider           | nice                | Creative: physically-based color from K, drives `color`.   |
| IES profile          | asset picker     | nice                | Creative: real-world light cookies.                        |
| light cookie/gobo    | texture picker   | nice                | Projected texture for spot lights.                         |

---

## ShadowComponent (single shadow map — spot/point)
| Field       | Widget   | Priority | Notes                                         |
| ----------- | -------- | -------- | --------------------------------------------- |
| `isActive`  | checkbox | needed   |                                               |
| resolution  | dropdown | needed   | 512/1024/2048/4096; rebuilds the `ShadowMap`. |
| depth bias  | drag     | nice     | Acne/peter-panning tuning.                    |
| normal bias | drag     | nice     |                                               |
| `mobility`  | dropdown | nice     | Update cadence hint.                          |

---

## CascadedShadowComponent (directional / CSM)
| Field                   | Widget           | Priority | Notes                               |
| ----------------------- | ---------------- | -------- | ----------------------------------- |
| `isActive`              | checkbox         | needed   |                                     |
| num cascades            | input (int)      | needed   | Rebuilds the `CascadedShadowMap`.   |
| lambda (split blend)    | slider           | needed   | 0 = uniform, 1 = logarithmic split. |
| resolution              | dropdown         | needed   | Per-cascade map size.               |
| depth/normal bias       | drag             | nice     |                                     |
| max shadow distance     | drag             | nice     |                                     |
| visualize cascades      | checkbox         | nice     | Debug tint per cascade.             |
| cascade split overrides | drag (per split) | nice     | Manual split distances.             |

---

## SkyboxComponent
| Field                 | Widget                       | Priority | Notes                                                    |
| --------------------- | ---------------------------- | -------- | -------------------------------------------------------- |
| `skyboxTexture`       | texture picker (cubemap/HDR) | needed   | Drag-drop; thumbnail.                                    |
| `skyIntensity`        | slider                       | needed   |                                                          |
| `isEnabled`           | checkbox                     | needed   |                                                          |
| rotation              | drag (yaw)                   | nice     | Rotate environment.                                      |
| procedural sky toggle | checkbox                     | nice     | Creative: switch to analytic sky (sun angle, turbidity). |
| tint                  | color picker                 | nice     |                                                          |

---

## FogComponent
| Field                | Widget       | Priority           | Notes                                                              |
| -------------------- | ------------ | ------------------ | ------------------------------------------------------------------ |
| `enabled`            | checkbox     | needed             |                                                                    |
| `type`               | dropdown     | needed             | Linear / Exponential / ExponentialSquared — drives visible fields. |
| `color`              | color picker | needed             |                                                                    |
| `density`            | slider       | needed (exp modes) | Small range, e.g. 0–0.2. Hide for Linear.                          |
| `start`              | drag         | needed (linear)    | Hide for exp modes.                                                |
| `end`                | drag         | needed (linear)    | Must stay > start.                                                 |
| height falloff       | drag         | nice               | Creative: height-based fog.                                        |
| sun/in-scatter color | color picker | nice               | Creative: volumetric look.                                         |
| volumetric toggle    | checkbox     | nice               | Creative: god-rays/light shafts.                                   |

---

## IndirectLightingComponent
Uses a `std::variant<monostate, AmbientSettings, DDGISettings>`. The technique dropdown should swap which sub-panel renders.

| Field         | Widget   | Priority | Notes                                      |
| ------------- | -------- | -------- | ------------------------------------------ |
| `enabled`     | checkbox | needed   |                                            |
| technique     | dropdown | needed   | None / Ambient / DDGI — swaps the variant. |
| `giIntensity` | slider   | needed   | Global multiplier.                         |

### AmbientSettings
| Field          | Widget       | Priority | Notes              |
| -------------- | ------------ | -------- | ------------------ |
| `ambientColor` | color picker | needed   | Flat ambient term. |

### DDGISettings
| Field                    | Widget           | Priority | Notes                               |
| ------------------------ | ---------------- | -------- | ----------------------------------- |
| `probeCount` (uvec3)     | vec3 input (int) | needed   | Grid dimensions.                    |
| `probeSpacing` (vec3)    | vec3 drag        | needed   | World-space spacing.                |
| `gridOrigin` (vec3)      | vec3 drag        | needed   | Or a "snap to scene bounds" button. |
| `raysPerProbe`           | dropdown/input   | needed   | Power-of-two values (64/128/256).   |
| `intensity`              | slider           | needed   |                                     |
| `visualizeProbes`        | checkbox         | needed   | Debug probe gizmos.                 |
| recalibrate/clear button | button           | nice     | Force probe re-relight.             |

---

## AnimationComponent (stub — creative proposal)
Currently empty. Suggested fields once skeletal/clip animation lands:

| Field               | Widget                         | Priority | Notes                                                             |
| ------------------- | ------------------------------ | -------- | ----------------------------------------------------------------- |
| current clip        | dropdown / asset picker        | needed   | Select animation clip.                                            |
| play / pause / stop | button row                     | needed   | Transport controls.                                               |
| playback speed      | slider                         | needed   | 0–2x.                                                             |
| loop                | checkbox                       | needed   |                                                                   |
| time scrubber       | slider                         | needed   | Scrub through current clip; show keyframes.                       |
| blend weight        | slider                         | nice     | For blending between clips.                                       |
| root motion         | checkbox                       | nice     |                                                                   |
| state machine       | curve/graph editor (new panel) | nice     | Animation blend-tree / state-machine editor as its own workspace. |
| event markers       | timeline markers               | nice     | Fire gameplay events at frames.                                   |

---

## Physics — RigidBodyComponent (`Entropy`)
| Field                  | Widget         | Priority | Notes                                                             |
| ---------------------- | -------------- | -------- | ----------------------------------------------------------------- |
| mass                   | drag           | needed   | Edits via `setMass()` (recomputes inertia). 0 ⇒ static/kinematic. |
| is static/kinematic    | checkbox       | needed   | Convenience for `invMass == 0`.                                   |
| `velocity`             | vec3 drag      | nice     | Mostly debug/initial conditions.                                  |
| `angularVelocity`      | vec3 drag      | nice     | Debug.                                                            |
| linear/angular damping | slider         | nice     | Common rigidbody param (add to struct).                           |
| gravity scale          | slider         | nice     | Per-body gravity multiplier (add to struct).                      |
| freeze position X/Y/Z  | checkbox group | nice     | The "add constraints later" comment — axis locks.                 |
| freeze rotation X/Y/Z  | checkbox group | nice     |                                                                   |
| restitution / friction | slider         | nice     | Material response (likely belongs on collider/material).          |

---

## Physics — Colliders (`ColliderBase` + primitives)
Shared base fields, then per-type shape fields. A **collider-type dropdown** that swaps the active collider struct is the key control.

### Shared (`ColliderBase`)
| Field | Widget | Priority | Notes |
|-------|--------|----------|-------|
| collider type | dropdown | needed | Sphere/AABB/OBB/Capsule/Cylinder/ConvexHull — swaps struct. |
| `isVisible` | checkbox | needed | Debug-draw the collider. |
| `localTransform` | vec3 drag ×2 | nice | Offset/rotation relative to entity. |
| "fit to mesh" | button | needed | The header comment explicitly wants this — auto-size from mesh bounds. |
| is trigger | checkbox | nice | No collision response, just overlap events (add to struct). |

### SphereCollider
| `center` vec3 drag (needed) · `radius` drag (needed) |

### AABBCollider
| `min` vec3 drag (needed) · `max` vec3 drag (needed) — or center+extents convenience |

### OBBCollider
| `center` vec3 drag (needed) · `extents` vec3 drag (needed) · `orientation` quat→euler drag (needed) |

### CapsuleCollider / CylinderCollider
| `start` vec3 drag (needed) · `end` vec3 drag (needed) · `radius` drag (needed) |

### ConvexHullCollider
| `vertices` — read-only count + "rebuild from mesh" button (needed). Per-vertex editing is impractical inline. |

---

## HierarchyComponent
Not a classic field editor, but the inspector can surface:

| Field | Widget | Priority | Notes |
|-------|--------|----------|-------|
| parent | asset/entity picker + label | nice | Shows parent name; drag an entity to reparent (`setParent`). |
| children | list | nice | Read-only list with click-to-select. |
| detach from parent | button | nice | Calls `removeFromParent`. |

(Primary hierarchy editing belongs in the Outliner via drag-drop, not the Properties panel.)

---

## BoundingBoxComponent
Mostly derived/read-only:

| Field | Widget | Priority | Notes |
|-------|--------|----------|-------|
| local min/max | vec3 (read-only) | nice | Display only. |
| world min/max | vec3 (read-only) | nice | Display only. |
| draw debug box | checkbox | nice | Toggle gizmo. |

---

## InstanceComponent / InstanceShapeComponent
Instancing data. Inline editing of thousands of instances isn't viable; expose summary + bulk controls.

| Field | Widget | Priority | Notes |
|-------|--------|----------|-------|
| instance count | label (read-only) | needed | |
| `color` (InstanceShape) | color picker | needed | |
| `useWireMode` (InstanceShape) | checkbox | needed | |
| add/remove instance | button | nice | Append/clear instances. |
| per-instance edit | new panel | nice | Open a dedicated instance table/scatter-paint tool for bulk transforms. |

---

## Cross-cutting editor UX notes
- **Conditional fields**: type dropdowns (light type, fog type, collider type, GI technique) must show/hide dependent fields.
- **Generation-aware writes**: `LightComponent` (and shadow `needsUpdate`) rely on generation counters — route edits through setters, not raw field writes, or the renderer won't refresh.
- **Asset pickers**: meshes, materials, textures, skyboxes all want drag-drop-from-content-browser targets with a thumbnail and a clear button.
- **Add/Remove Component**: a "+" menu on the Properties panel to attach any of the above to the selected entity.
- **Multi-edit**: editing a field with multiple entities selected should apply to all (later polish).
