# GroundTruthAmbientOcclusionPass

**Source: `Engine/src/renderer/passes/GroundTruthAmbientOcclusionPass.h/.cpp`**

Screen-space ambient occlusion after Jimenez et al., *Practical Realtime Strategies for Accurate
Indirect Occlusion* (SIGGRAPH 2016). A [[ComputePass]] that runs between [[HiZPass]] and
[[StochasticScreenSpaceReflectionsPass]], producing occlusion and a bent normal for
[[LightingPass]]. Cross-refs: [[GBufferPass]], [[RenderPass]].

---

## What makes it "ground truth"

Horizon-based methods before it (HBAO and its descendants) find the horizon along a screen-space
slice and then *average* samples to estimate visibility. GTAO finds the same horizons and feeds them
to a closed-form solution of the cosine-weighted visibility integral over the arc they leave open.
The sampling therefore only has to locate the horizon; the integration is exact. That is the whole
of the difference, and it is why the result converges to a reference path trace rather than to a
tuned approximation.

`sliceVisibility` in `GroundTruthAmbientOcclusion.cs.glsl` is that integral:

```
0.25 * (-cos(2h1 - n) + cos(n) + 2*h1*sin(n))
  + 0.25 * (-cos(2h2 - n) + cos(n) + 2*h2*sin(n))
```

where `h1`/`h2` are the two horizon angles of the slice and `n` is the angle of the surface normal,
all measured from the view vector and all within the slice plane.

## The slice frame

A slice is a *line* through the pixel, not a ray, which is why the directions span only half a turn
(`phi = (slice/sliceCount + rotation) * PI`). The plane containing that line and the view vector is
where all the angles live:

- `marchDirection = vec3(direction.x / proj[0][0], direction.y / proj[1][1], 0)` — the view-space
  direction the screen-space march corresponds to. Reusing the screen direction directly is wrong:
  under Vulkan's downward y and a non-square aspect the two disagree, and a mirrored frame swaps the
  two horizons and reflects every bent normal. Visibility survives that (the integral is invariant
  under `(h1, h2, n) -> (-h2, -h1, -n)`), the bent normal does not.
- `slicePlaneNormal = normalize(cross(marchDirection, view))` — the axis angles rotate about.
- `tangent = normalize(cross(view, slicePlaneNormal))` — in-plane, perpendicular to the view vector,
  pointing the way the positive march walks. The order matters: `cross(slicePlaneNormal, view)`
  gives the *opposite* side, which silently mirrors every bent normal.
- `n = atan(dot(projectedNormal, tangent), dot(projectedNormal, view))` — the signed reference angle.
- `projectedNormalLength` weights the slice. It is the Jacobian of projecting the hemisphere onto the
  slice plane, not a heuristic.

## The horizon starts at the hemisphere limit

Both horizons are initialised to where the normal's hemisphere and the camera's overlap:

```glsl
float lowHorizonCos1 = cos(max(-HALF_PI, n - HALF_PI));
float lowHorizonCos2 = cos(min(HALF_PI, n + HALF_PI));
```

Screen space holds no record of anything behind the view vector, so the arc can never be wider than
`[-PI/2, PI/2]` around the view, and it can never be wider than `[n - PI/2, n + PI/2]` around the
normal either. Taking the tighter of the two up front means samples only ever close the horizon
further and no separate clamp step is needed after the march.

A consequence worth knowing: at grazing angles the normal's hemisphere sticks out past the camera's,
the arc is truncated, and the visibility of a flat unoccluded surface comes out slightly below 1
(about 0.785 at the extreme). That is inherent to the method, not a bug in this implementation.

## Bent normal

`sliceBentDirection` returns the centroid of the same arc, as `alongTangent * tangent + alongView *
view`. Accumulated across slices and normalised it gives the direction the surface is actually open
towards — the information a single visibility number throws away.

Sign check for the view component, since it is easy to get backwards: with `n = 0`, `h1 = -PI/2`,
`h2 = PI/2` (flat, unoccluded, normal facing the camera), `alongTangent` evaluates to 0 and
`alongView` to `8/12 = 2/3`, positive. The bent normal is therefore `+view`, which is the normal.

Stored in rgb of the output, occlusion in alpha, `RGBA16F`.

## Noise and filtering

One frame traces `sliceCount * stepCount * 2` samples per pixel (3 x 4 x 2 by default), which is far
too few on its own. Three things make up the difference, and all three are the paper's:

1. **Spatial variation.** `spatialDirectionNoise` / `spatialOffsetNoise` give every pixel in a 4x4
   block a different starting direction and a different sub-step offset.
2. **Temporal variation.** `TEMPORAL_ROTATIONS[6]` and `TEMPORAL_OFFSETS[4]` cycle those over 24
   frames, so a pixel does not repeat itself inside the window the accumulation covers.
3. **A filter that undoes both.** `GroundTruthAmbientOcclusionDenoise.cs.glsl` averages the 3x3
   neighbourhood — which is not blurring an estimate, it is *finishing* one, because the neighbours
   traced slices this pixel never did — and then accumulates along the motion vector.

The spatial and temporal filters share one dispatch. Splitting them would need a third full-screen
`RGBA16F` per frame in flight for no benefit.

## Reprojection validity

Occlusion belongs to the surface rather than to anything seen through it, so unlike a reflection
(see [[StochasticScreenSpaceReflectionsPass]], which has to reproject a virtual point) it travels
with the surface's own motion vector.

History is validated by depth rather than by a neighbourhood clamp. A perspective clip `w` *is* the
distance in front of the camera, so `(prevViewProj * vec4(worldPos, 1)).w` is the depth the history
must have if the reprojection landed on the same surface, and it is compared against
`historyLinearDepth` — mip 0 of the previous frame's [[HiZPass]] pyramid.

The expected depth is camera motion only, so a surface that moved under a still camera fails the
test and restarts its accumulation. That is the safe direction: it costs noise on movers rather than
dragging their occlusion across the floor behind them.

A variance clamp was deliberately *not* added. The signal reaching the temporal blend is already
spatially filtered and low frequency, so a bound built from its own neighbourhood would mostly put
back the noise the pass just removed, and the failure it guards against is a disocclusion the depth
test already catches.

## Why not the Hi-Z pyramid

[[HiZPass]] reduces with `min`, so a coarse mip reports the *nearest* surface under it. Sampling
that for a horizon search would systematically pull horizons towards the view and over-occlude
everything. Only mip 0, the plain linear depth, is read. Wide radii are bounded by
`m_maxScreenRadius` instead.

## Parameters

| Member | Default | What it is |
| --- | --- | --- |
| `m_radius` | 1.0 | How far from a surface geometry still occludes it, world units |
| `m_maxScreenRadius` | 128.0 | Cap on what that radius may cover in pixels |
| `m_falloffRange` | 0.25 | Fraction of the radius the fade to no contribution spans |
| `m_sliceCount` | 3 | Slice directions per pixel per frame |
| `m_stepCount` | 4 | Horizon samples per direction per side |
| `m_depthRejection` | 0.05 | Relative depth difference separating one surface from another |
| `m_hysteresis` | 0.9 | Weight of the previous frame's accumulation |

Clamping the screen radius changes the world distance it stands for, so `worldRadius` is recovered
from the clamped value before the falloff is derived. Otherwise an occluder would fade out somewhere
other than where sampling stops.

## Consumption

[[LightingPass]] multiplies the alpha into `ao` behind `RENDER_USE_AMBIENT_OCCLUSION`. The material's
own occlusion channel stays in the product: it is baked from the mesh and describes its cavities,
this term describes the room the mesh was put in, and the two occlude different scales.

Debug views go through [[CompositePass]], not the lighting shader:
`RENDER_SHOW_AMBIENT_OCCLUSION` (alpha) and `RENDER_SHOW_BENT_NORMALS` (rgb, remapped).

## Known gaps

- **No thin-occluder compensation.** A depth buffer records surfaces, not solids, and the horizon
  search treats every one as infinitely thick. Railings, foliage and thin props therefore cast more
  occlusion than they should.
- **The bent normal is produced but not yet consumed.** Specular occlusion is the reason it exists;
  see the note in [[LightingPass]].
- **No half-resolution path.** Every dispatch is full resolution.
