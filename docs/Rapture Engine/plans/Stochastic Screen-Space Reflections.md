# Stochastic Screen-Space Reflections

**Related: [[G-Buffer Expansion]], [[Ray-Traced Reflections]], [[Image-Based Lighting]], [[DDGI]], [[LightingPass]], [[Rendering and GI Roadmap]]**

Implementation plan for SSR following Stachowiak & Uludag, *Stochastic Screen-Space Reflections*
(SIGGRAPH 2015 Advances in Real-Time Rendering). Covers the roughness range the DDGI field cannot:
the field handles rough/glossy for free, this handles the sharp end.

**Prerequisite: [[G-Buffer Expansion]] steps 1-6 — all landed.** This plan assumes a linear HDR scene
colour target, motion vectors, a mipped history colour buffer, and `R32F`. The mip chain is the one
piece outstanding, and it is not needed until step 4 below.

Zero-assumption: current-state claims are cited to source.

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
| Trace | half | hit UV, hit depth, PDF, hit mask | `RGBA16F` |
| Resolve | full | BRDF-weighted radiance + avg hit distance | `RGBA16F` |
| Temporal | full | accumulated radiance | `RGBA16F` |
| Composite | full | — | folded into `DeferredLighting.fs.glsl` |

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
3. **Importance-sample the GGX lobe** with a Halton sequence offset by frame index. Rays that go
   below the surface are **regenerated**, not clamped.
4. **Sampling bias** — this is cheap and high-impact, do not skip it:
   ```glsl
   vec2 u = halton(sampleIdx);
   u.x = mix(u.x, 0.0, bias);   // bias ~0.7
   importanceSample(u);
   ```
   **The direction depends on the parameterisation.** With the VNDF sampler in
   `common/ImportanceSampling.glsl`, `u.x` drives `r = sqrt(u.x)`, the radius on the projected
   disk, so **0** is the mirror end and the bias pulls toward zero. A plain NDF sampler that maps
   `u.x` to `cosTheta` runs the other way. Getting this backwards widens the lobe instead of
   narrowing it, which adds variance rather than removing it.
   Shifting samples toward the mirror direction kills the BRDF-tail noise. The truncated
   distribution has a different normalisation constant, but the resolve's weight normalisation
   (§3.3) re-normalises it automatically, so this stays energy-correct.
5. March. Two paths chosen by roughness:
   - **smooth** → stackless Hi-Z walk: `while (level > -1) { step through cell; above Z plane →
     ++level; below → --level; }`
   - **rough** → cheap linear march. May skip thin geometry; irrelevant when it is about to be
     blurred.
6. Write **hit UV + hit depth + PDF + hit mask**. Deliberately *not* colour — the resolve needs
   the hit location so each full-res pixel can shade it against its own BRDF.

### 3.3 Resolve (full-res) — the core of the technique

Each full-res pixel gathers the **hit points of its neighbours** and reuses them, assuming shared
visibility. Naively weighting by `localBRDF/originalPDF` produces spikes whenever a neighbour has
a very different normal or roughness.

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
point — a log fit over roughness, hit distance, and grazing-angle elongation. No actual cone
tracing. **Contact hardening falls out for free:** short hit distance → tight footprint → low mip
→ sharp reflection.

The elongation correction is a one-liner and matters a lot at grazing angles, where a cone is a
poor fit to an anisotropic lobe:
```glsl
specularConeTangent *= mix(saturate(NdotV * 2.0), 1.0, sqrt(roughness));
```

Neighbourhood is 4 samples (2×2), jittered temporally — reusing rays across a fixed 2×2 quad
creates 2×2 blocks that read as *features* rather than noise and make temporal AA misbehave.

Also write the **average hit distance** to the alpha channel; §3.4 needs it.

### 3.4 Temporal

Reproject along **reflection depth**, not G-buffer depth. Reprojecting the surface smears
reflections, because the reflected image moves with *parallax*, not with the surface's motion
vector. Use the virtual reflected position `worldPos + reflectDir * hitT`, with `hitT` from the
resolve's alpha channel.

Then TAA-style **neighbourhood clamping** with a deliberately **expanded** colour bounding box,
tuned to prefer some smearing over noise. The reflection colour is inherently a frame late (it
comes from the history buffer), so lag cannot be fully removed regardless.

### 3.5 Composite

Back in `DeferredLighting.fs.glsl`. The current field-only specular is at `:531-538`:

```glsl
vec3 R = reflect(-V, N);
vec3 Rd = getSpecularDominantDir(N, R, roughness);
vec3 prefilteredRadiance = getIrradiance(fragPos, N, Rd, V, u_DDGI_Volume) / PI;
indirectSpecular = prefilteredRadiance * F * ao;
```

Replace with a roughness-driven blend:

- `radiance = mix(ddgiField, ssrResult, ssrWeight)` where `ssrWeight` falls off with roughness and
  with SSR confidence (screen-edge fade, hit mask, disocclusion).
- **Then** multiply by the pre-integrated `FG` from the BRDF LUT. Applying `FG` *after* the
  temporal filter reduces smearing and noise — this is explicit in the talk's bonus slides.

**Fallback chain, on SSR miss or off-screen:** SSR → DDGI field → IBL prefiltered cube. The DDGI
field is the better fallback because it is spatially varying; the IBL cube covers rays that also
leave the DDGI volume.

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

**Push constant budget:** `LightingPushConstants` reached exactly 128 bytes — the guaranteed limit —
before fog was removed to make room. It now sits at 104. The next few handles fit; after that they
belong in a UBO rather than push constants.

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
2. **Hi-Z pyramid** build pass (§3.1). Verify by visualising mip levels. The `ComputePass` base it
   builds on now exists; this is its first caller, so expect the resource declaration to need
   adjusting once a real dispatch exercises it. Allocate it **per frame-in-flight** — mip 0 is the
   linear view depth the temporal pass needs from the previous frame.
3. **Trace pass, half-res, linear march only** — no Hi-Z path, no bias, no importance sampling
   (pure mirror ray). Output hit UV. Debug-view it directly; this proves the marching and the
   world-position reconstruction.
4. **Naive resolve** — sample history colour at the hit UV, mip 0, no weighting. A recognisable,
   noisy mirror reflection. Composite it with a hard roughness cutoff.
5. **GGX importance sampling + PDF output** in the trace; **BRDF-weighted normalised resolve**
   (§3.3). This is the step where it starts looking correct rather than just working.
6. **Colour mip chain + filtered importance sampling** including the elongation fix. Contact
   hardening appears here.
7. **Sampling bias 0.7** + matching filter bias.
8. **Temporal** with reflection-depth reprojection and expanded neighbourhood clamping (§3.4).
9. **Hi-Z trace path** for low-roughness pixels, selected by roughness.
10. **Full composite**: roughness blend against the DDGI field, `FG` after temporal, edge/miss
    fade to the fallback chain.
11. *(optional, perf only)* tile classification + ray allocation + indirect dispatch.

Steps 3–5 are the ones worth the most debugging patience; if the resolve is wrong, everything
downstream looks like noise and it is tempting to blame the trace.
