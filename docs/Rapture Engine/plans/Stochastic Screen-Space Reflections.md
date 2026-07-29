# Stochastic Screen-Space Reflections

**Related: [[G-Buffer Expansion]], [[Ray-Traced Reflections]], [[Image-Based Lighting]], [[DDGI]], [[LightingPass]], [[Rendering and GI Roadmap]]**

Implementation plan for SSR following Stachowiak & Uludag, *Stochastic Screen-Space Reflections*
(SIGGRAPH 2015 Advances in Real-Time Rendering). Covers the roughness range the DDGI field cannot:
the field handles rough/glossy for free, this handles the sharp end.

**Prerequisite: [[G-Buffer Expansion]] steps 1-6 — all landed.** This plan assumes a linear HDR scene
colour target, motion vectors, a mipped history colour buffer, and `R32F`. The mip chain is the one
piece outstanding, and it is not needed until step 4 below.

Zero-assumption: current-state claims are cited to source.

> **This document has been wrong before.** It was written before the deck was on hand and several
> claims were reconstructed rather than read. The code faithfully implemented at least three of
> them and the resulting bugs cost days: the `RGBA16F` trace record (§2), the sampling-bias
> direction (§3.2 step 4), and a cone-tangent formula presented as though the talk supplied one
> (§3.3). Sections now marked as ours-not-the-talk's are marked that way deliberately. The deck
> itself is the source of truth; where this doc and the deck disagree, the deck wins.

---

## 1. Why this technique

Classic SSR shoots one mirror ray and blurs the result by roughness. That gives no **contact
hardening** (blur is uniform regardless of how far the reflected object is) and no per-pixel
normal response. Killzone-style pre-blurred variants have the same ceiling.

The insight of this talk: treat SSR as a **Monte Carlo estimator of the specular integral** and
spend the whole budget on variance reduction rather than on more rays. One ray per half-res pixel
is enough if the resolve is smart.

Note: **AMD FidelityFX SSSR** is a descendant of this same talk (Uludag later went to AMD). Same
skeleton, but it uses blue noise instead of Halton and replaces the colour-mip filtered importance
sampling with a heavier spatial denoiser. Useful only as a cross-check on parameter choices —
we implement from the talk.

---

## 2. Pipeline

The talk's full breakdown is six passes. **We skip tile classification and ray allocation in v1** —
they are a performance optimisation (variable ray count per tile driven by 1/8-res tracer rays),
not a quality feature. A fixed one ray per half-res pixel produces the same image at higher cost
and removes two passes.

```
  Hi-Z build  ──►  Trace (half-res)  ──►  Resolve (full-res)  ──►  Temporal  ──►  Composite
   compute          compute               compute                  compute        in lighting
```

| Pass | Res | Output | Format |
|---|---|---|---|
| Hi-Z build | full → 1×1 | min-Z pyramid over linear view depth | `R32F` + mips |
| Trace | half | hit UV, hit depth, PDF | `RGBA32F` |
| Resolve | full | BRDF-weighted radiance + avg hit distance | `RGBA16F` |
| Temporal | full | accumulated radiance + validity | `RGBA16F` |
| Composite | full | — | folded into `DeferredLighting.fs.glsl` |

**The trace record must be 32-bit, and this is not a nicety.** An earlier revision of this plan
specified `RGBA16F` and the code followed it. Two things break at half precision:

- The PDF spans orders of magnitude. Head-on it is roughly `1/(4π·roughness⁴)`, which crosses the
  half-float ceiling of 65504 at **roughness ≈ 0.033** and reaches `5×10⁵` at the `MIN_GGX_ROUGHNESS`
  floor of 0.02. Stored as `+inf`, every weight in the resolve becomes `f/inf = 0`, `weightSum`
  stays zero, and the pixel writes black. Every surface smoother than ~0.033 silently produced no
  reflection at all.
- A hit UV near 1.0 has a half-float spacing of `2⁻¹¹`, which at 1920 px is most of a pixel. Mirror
  reflections lost sub-pixel precision progressively toward the right and bottom of the screen.

No separate hit mask is stored: a PDF above zero marks the record usable. The resolve's alpha
follows the same convention in reverse — a **negative** average hit distance means no ray backed
this pixel, since a real distance never is.

---

## 3. Pass detail

### 3.1 Hi-Z build

A min-Z pyramid over **linear view depth**, not raw `D24S8`. Each mip takes the min of its four
parents. Straightforward compute reduction; the existing `Flatten2dArray.cs.glsl` /
`FlattenDepthArray.cs.glsl` compute passes are the closest local reference for the dispatch shape.

Also independently useful for SSAO and GPU occlusion culling — see [[Rendering and GI Roadmap]]
line 100.

### 3.2 Trace (half-res, 1 ray/pixel)

Per half-res pixel:

1. Read depth; background (`depth >= 1.0`, matching `DeferredLighting.fs.glsl:410`) → write miss.
2. Reconstruct world position via `invViewProj` (`DeferredLighting.fs.glsl:417-420`), read normal
   from RT0 and roughness from RT2.
3. **Importance-sample the GGX lobe**, offset by frame index. The talk uses a Halton sequence and
   plain NDF sampling, listing VNDF [Heitz14] only as "could try"; we use Hammersley and the Heitz
   2018 VNDF warp, so anything the talk says about the sample parameterisation has to be
   re-derived rather than copied (see step 4).
   Rays that go below the surface are **regenerated**, not clamped. The regeneration step has to
   actually move within the sequence — stepping by the sequence length lands on the same sample
   every attempt and silently turns the retry loop into a no-op that drops the ray.
4. **Sampling bias** — this is cheap and high-impact, do not skip it:
   ```glsl
   vec2 u = halton(sampleIdx);
   u.x = mix(u.x, 0.0, bias);   // bias ~0.7
   importanceSample(u);
   ```
   **The direction depends on the parameterisation.** The talk's own line is
   `u.x = lerp(u.x, 1.0, bias)` (slide 67) — correct for *their* NDF sampler, and wrong for ours.
   With the VNDF sampler in `common/ImportanceSampling.glsl`, `u.x` drives `r = sqrt(u.x)`, the
   radius on the projected disk, so **0** is the mirror end and the bias pulls toward zero. Getting
   this backwards widens the lobe instead of narrowing it, which adds variance rather than removing
   it.
   Shifting samples toward the mirror direction kills the BRDF-tail noise. The truncated
   distribution has a different normalisation constant, but the resolve's weight normalisation
   (§3.3) re-normalises it automatically, so this stays energy-correct. The PDF written to the hit
   record must therefore be the **unbiased** one — slide 67 is explicit ("still need accurate PDF
   values… our variance reduction re-normalizes").

   **Matching filter bias** (slide 69, "counter sampling bias with filter bias, same parameter").
   The scale is `sqrt(1 - bias)`, not `1 - bias`: the truncation scales `u.x`, and the sampler takes
   its square root to get the disk radius, so the radius — and with it the half-vector slope the
   footprint has to match — shrinks by the root. Using `1 - bias` makes the filter 1.8× narrower
   than the lobe actually sampled, which is systematic under-blurring that the temporal filter then
   has to absorb with an inflated hysteresis.
5. March. Two paths chosen by roughness (the talk's threshold is **20%**, slide 73):
   - **smooth** → stackless Hi-Z walk: `while (level > -1) { step through cell; above Z plane →
     ++level; below → --level; }`
   - **rough** → cheap linear march. May skip thin geometry; irrelevant when it is about to be
     blurred.

   **The Hi-Z walk is only valid for a ray whose depth increases.** A min-Z pyramid bounds what
   lies *beyond* a cell, so a ray heading back toward the eye never crosses a cell's depth plane:
   it descends a level every iteration without advancing, exits at level -1 still sitting on its
   own origin, and passes the terminal thickness test against the surface it started on. The
   result is a reported hit at the pixel's own UV — the surface reflects itself. This is not an
   edge case; any surface with `N ≈ V` reflects straight back at the viewer, which is exactly the
   mirror-smooth range the Hi-Z path exists to serve. Route those rays to the linear march, whose
   crossing test works in either direction. Neither the talk's slide 40 pseudocode nor the
   paraphrase above mentions this precondition.

   The cell-boundary crossing epsilon must be expressed **as a fraction of the current cell**, not
   as a fixed UV distance. A constant that is a rounding error at coarse levels is most of a texel
   at mip 0, and overshooting there skips the cells the walk exists to visit.
6. Write **hit UV + hit depth + PDF + hit mask**. Deliberately *not* colour — the resolve needs
   the hit location so each full-res pixel can shade it against its own BRDF.

### 3.3 Resolve (full-res) — the core of the technique

Each full-res pixel gathers the **hit points of its neighbours** and reuses them, assuming shared
visibility. The talk is explicit that this assumption is simply taken: *"Visibility **might** be
different. We **assume** it's the same"* (slide 45). Naively weighting by `localBRDF/originalPDF`
produces spikes whenever a neighbour has a very different normal or roughness.

**The normalisation is the entire fix, and nothing else is warranted.** Do not add normal or
roughness similarity tests to reject mismatched neighbours — the talk has none, and they do active
harm: they drop taps from both sums, cutting the effective sample count to as low as one near any
silhouette or normal-mapped detail, and when all four are rejected the pixel writes an empty record
that the temporal pass then has to paper over. A per-tap weight ceiling is worse than useless:
because the same clamped weight appears in both the numerator and the denominator, the result is a
convex combination of the tap radiances and **cannot** exceed the brightest of them. Clamping only
changes which neighbour dominates, dragging the estimator toward a uniform average and destroying
the per-pixel normal and roughness response the technique exists to preserve.

If taps appear to spike, the cause is upstream — an unusable PDF is the usual one.

The fix, from the talk's derivation: multiply and divide the specular integral by
`∫ fs·cosθ dl`, **pre-integrate one copy** (that is exactly the split-sum `FG` env-BRDF term) and
Monte-Carlo the other:

```
result = Σ(color_k · w_k) / Σ(w_k),   w_k = localBrdf(hit_k) / pdf_k
```

```glsl
result = 0.0; weightSum = 0.0;
for (pixel in neighborhood) {
    weight  = localBrdf(pixel.hit) / pixel.hitPdf;
    result += color(pixel) * weight;
    weightSum += weight;
}
result /= weightSum;
```

The `/weightSum` normalisation is what makes the whole thing viable — compare the talk's
"no normalization" and "with normalization" slides. **`FG` is applied later, at composite, not
here** (§3.5).

**Filtered importance sampling.** `color(pixel)` samples the *mipped* history colour buffer, not
mip 0. The mip is chosen from an analytic estimate of the specular cone footprint at the hit
point. No actual cone tracing. **Contact hardening falls out for free:** short hit distance →
tight footprint → low mip → sharp reflection. This is also what should keep *rough* surfaces
quiet: a wide lobe selects a high mip, so each tap arrives already prefiltered over the area the
lobe covers. Visible noise that grows with roughness means the footprint is undersized, not that
the technique is working as intended.

Slide 68 says only "mip determined by **log function fit** — roughness, distance to hit,
elongation". **No formula is given.** The base cone tangent in `coneFootprintMip`
(`alpha / (1 - alpha)`) is ours, not the talk's, and is the first suspect for footprint problems.

The elongation correction *is* the talk's, verbatim from bonus slide 84 ("found a close fit in
Mathematica, also came up with an ad-hoc one, ad-hoc close enough in testing"). It matters a lot
at grazing angles, where a cone is a poor fit to an anisotropic lobe:
```glsl
specularConeTangent *= mix(saturate(NdotV * 2.0), 1.0, sqrt(roughness));
```

**The colour lookup must be reprojected.** The hit UV is a position on *this* frame's screen, but
the only lit colour available is the previous frame's (slides 12 and 89 both say so). Sampling the
history buffer at the raw hit UV makes every reflection slide against the object it is reflecting
by the full screen-space motion at the hit point. Carry the hit back along the motion vector stored
at it first.

Neighbourhood is 4 samples (2×2), jittered temporally — reusing rays across a fixed 2×2 quad
creates 2×2 blocks that read as *features* rather than noise and make temporal AA misbehave.

Also write the **average hit distance** to the alpha channel; §3.4 needs it.

### 3.4 Temporal

Reproject along **reflection depth**, not G-buffer depth. Reprojecting the surface smears
reflections, because the reflected image moves with *parallax*, not with the surface's motion
vector. Use the virtual reflected position `worldPos + reflectDir * hitT`, with `hitT` from the
resolve's alpha channel.

`hitT` must be the **average over the local rays** (slide 66), which is why the resolve computes
it. Reading this pixel's own half-res ray instead is a redraw every frame: the distance swings with
each new sample, and a miss collapses it to zero, which silently reverts that pixel to G-buffer
reprojection — the exact smearing this step exists to remove. Since roughly half of all pixels miss
on any given frame, that alternation happens at the sampling rate.

Then TAA-style **neighbourhood clamping** with a deliberately **expanded** bound (slide 89), tuned
to prefer some smearing over noise. The reflection colour is inherently a frame late (it comes from
the history buffer), so lag cannot be fully removed regardless.

Build the bound from the neighbourhood's **mean and standard deviation**, not its min and max. The
talk cites [Karis14] for the temporal, and this is his method. A min/max box has a specific failure
here: one stray bright sample sets the extremes for all nine of its neighbours, each of which is
then licensed to keep history that bright, so the outlier grows into a blob instead of averaging
away. Speckles getting *larger* rather than fewer as the temporal runs is the signature.

Three failure modes to avoid in the blend itself, all of which read as unbounded smearing:

- **Never let both sides of the mix be the same value.** Falling back to `clampedHistory` as the
  target when the current sample is invalid makes `mix(h, h, k)` return `h` for any `k` — the pixel
  freezes permanently and is then dragged around by reprojection. A pixel with no rays behind it
  must decay, not hold.
- **Clamp the alpha channel too, or make it a convex blend.** Clamping only `rgb` leaves alpha as an
  unbounded IIR over a moving bilinear fetch, and it will smear on its own.
- **Drop history where the neighbourhood cannot vouch for it.** If no neighbour produced an
  estimate there is nothing to clamp against and no evidence the accumulation still describes this
  pixel; keeping it is what drags a reflection along behind the camera.

Reprojection currently uses camera motion only, so **dynamic geometry reprojects wrong** — both a
moving reflector and a moving reflected object. Invisible in a static scene.

### 3.5 Composite

Back in `DeferredLighting.fs.glsl`. Indirect specular is the IBL prefiltered cube sampled along the
dominant reflection direction, times the split-sum weight:

```glsl
vec3 R = reflect(-V, N);
vec3 Rd = getSpecularDominantDir(N, R, roughness);
vec3 prefilteredRadiance = textureLod(gCubemaps[pc.prefilteredEnvHandle], Rd,
                                      roughness * (pc.prefilteredEnvMipCount - 1.0)).rgb;
indirectSpecular = prefilteredRadiance * specularWeight * ao;
```

The mip lookup is **linear in roughness** because `ImageBasedLighting.cpp` bakes mip `i` at
`roughness = i / (mips - 1)`. An earlier `sqrt(roughness)` mapping was over-blurring every glossy
surface by roughly an octave of roughness.

Blend SSR over that:

- `radiance = mix(fallback, ssrResult, ssrWeight)` where `ssrWeight` falls off with roughness and
  with SSR validity (miss, off-screen, disocclusion).
- **Then** multiply by the pre-integrated `FG` from the BRDF LUT. Applying `FG` *after* the
  temporal filter reduces smearing and noise — explicit on slide 86.

**Fallback chain, on SSR miss or off-screen:** SSR → IBL prefiltered cube. The DDGI field was tried
as an intermediate fallback and removed: it is a diffuse irradiance probe lookup divided by π, far
too low-frequency to stand in for a reflection, and blending it in costs two tuned crossover
constants. Its one advantage is spatial variation and therefore local occlusion, so if a rough
surface in an enclosed space visibly reflects sky it should not see, that is the trade to revisit.

**This is a feedback path.** The resolve samples the composited scene colour, so once SSR is
composited into it, frame *n*'s reflections contain frame *n-1*'s reflections and a bounce is added
every frame. Loop gain is roughly `specularWeight × ssrWeight` — negligible for dielectrics, near
unity for a smooth metal, where it converges slowly enough to read as an infinite mirror. Either
attenuate the history colour or keep a reflection-free copy of the scene for SSR to sample.

#### Debug views must not be written into the scene colour

Anything drawn into `sceneColorHdr` is what SSR reflects next frame. A debug visualisation written
from `DeferredLighting.fs.glsl` therefore becomes reflected content, and because such views are
mostly black, the reflection of the visualisation darkens the visualisation, compounding to solid
black over a few frames. This looks exactly like an SSR bug and is not one.

Texture-inspection views belong in `CompositePass`, which writes the presented image that nothing
reads back. Views that genuinely change the *lighting* (direct/indirect/GI toggles) stay in the
lighting shader, because changing the scene is their purpose. `common/RenderFlags.glsl` mirrors
`RenderSettings.h` and is shared by both shaders rather than duplicated.

---

## 4. The BRDF LUT — **wired**

`ImageBasedLighting` (`Engine/src/renderer/ImageBasedLighting.h`) bakes a split-sum BRDF integration
LUT alongside the irradiance and prefiltered cubes and exposes `getBrdfLutBindlessIndex()`. It used to
never reach the lighting shader, which relied on `fresnelSchlickRoughness` (`common/BRDF.glsl:18`)
alone for the indirect specular weight.

`DeferredLighting.fs.glsl` now samples it at `(NdotV, roughness)` and applies `F0 * a + b`. Because the
LUT integrates the **full** GGX lobe including the geometry/masking term that the Fresnel-only
approximation omits, rough metals get dimmer — the previous behaviour was over-energetic. Smooth
surfaces are close to unchanged.

`getBrdfLutBindlessIndex()` returns 0 before the bake completes and the shader samples it
unconditionally, so the first frames of a scene read whatever occupies bindless slot 0.

This is the same `FG` term applied after the temporal pass (§3.5), so it is shared, not duplicated.

---

## 5. Resolution and cost

The talk's PS4 numbers (1600×900, all pixels reflective, HQ rays below 20% roughness, bias 0.7):
**2.19 ms total** for 4 resolve samples / 1 ray per half-res pixel — 0.20 linear trace, 0.37 Hi-Z
trace, 0.81 resolve, 0.30 temporal, plus 0.40 for the two classification passes we are skipping.

Note the counter-intuitive row: **1 resolve sample with 4 rays costs 4.41 ms and looks worse than
4 resolve samples with 1 ray at 2.19 ms.** Resolve samples are much cheaper than rays. This is the
single most important budgeting fact in the talk — do not "improve quality" by adding rays.

---

## 6. Relationship to RT reflections

[[Ray-Traced Reflections]] is the other half of this and the two are complementary, not competing:

- SSR is cheap and detailed but limited to what is on screen; it fails at screen edges, on
  backfaces, and for anything off-camera.
- RT reflections use the TLAS (`RtInstanceData`) via ray query, which the engine already does for
  DDGI (`ddgi/ProbeTrace.cs.glsl:6`), and have none of those limits at higher cost.

The eventual arrangement is SSR first, RT as the fallback where SSR has no answer — which is
exactly the fallback slot the DDGI field occupies in §3.5. Both plans converge on the same
composite point, and both need the same prerequisites (motion vectors, temporal, BRDF LUT), so
[[G-Buffer Expansion]] serves both.

---

## 7. Build order

1. ✅ **Wire the BRDF LUT** into `DeferredLighting.fs.glsl` (§4). Independent, cheap, immediately
   improves ambient specular.
2. ✅ **Hi-Z pyramid** build pass (§3.1). Allocated **per frame-in-flight** — mip 0 is the linear
   view depth the temporal pass needs from the previous frame.
3. ✅ **Trace pass, half-res, linear march only** — no Hi-Z path, no bias, no importance sampling
   (pure mirror ray). Output hit UV. Debug-view it directly; this proves the marching and the
   world-position reconstruction.
4. ✅ **Naive resolve** — sample history colour at the hit UV, mip 0, no weighting. A recognisable,
   noisy mirror reflection.
5. ✅ **GGX importance sampling + PDF output** in the trace; **BRDF-weighted normalised resolve**
   (§3.3). This is the step where it starts looking correct rather than just working.
6. ✅ **Colour mip chain + filtered importance sampling** including the elongation fix. Contact
   hardening appears here.
7. ✅ **Sampling bias 0.7** + matching filter bias.
8. ✅ **Temporal** with reflection-depth reprojection and neighbourhood clamping (§3.4).
9. ✅ **Hi-Z trace path** for low-roughness pixels, selected by roughness.
10. **Full composite**: roughness blend against the fallback, `FG` after temporal, miss/edge fade,
    and a decision on the history feedback path (§3.5).
11. *(optional, perf only)* tile classification + ray allocation + indirect dispatch. Note this is
    also the talk's answer to noise on rough surfaces, since it allocates more rays where
    perceptual variance is high — so it is not purely a performance step.

Steps 3–5 are the ones worth the most debugging patience; if the resolve is wrong, everything
downstream looks like noise and it is tempting to blame the trace.

### Known remaining defects

- `thickness` stops having any effect past roughly step 30. The acceptance window is
  `max(thickness, stepSize)`, and with 1.05 geometric growth over 64 steps to 40 units, `stepSize`
  reaches ~2 world units — so the far half of every ray is governed by the step, not the parameter.
- Temporal reprojection is camera-only; dynamic geometry reprojects wrong (§3.4).
- `RAY_ORIGIN_BIAS` (0.005 of view depth) was partly compensating for the Hi-Z self-hit above and
  wants re-tuning now that the real cause is fixed. It should be expressed relative to the depth
  buffer's texel footprint rather than as a bare fraction of depth.
- At the image border the resolve's 2×2 window clamps, so up to three of four taps collapse onto the
  same texel and a 1-tap estimate is presented as a 4-tap one.
- `hysteresis` (0.92) was tuned against a pipeline with an undersized cone, an unusable PDF and a
  freezing temporal. It should come down.
