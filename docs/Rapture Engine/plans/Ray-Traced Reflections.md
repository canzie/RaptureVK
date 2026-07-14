# Ray-Traced Reflections

Plan for adding sharp, ray-traced specular reflections on top of the existing rough DDGI
specular term. The DDGI field covers the rough/glossy end; this adds a traced source for the
low-roughness end and blends the two by roughness. Zero-assumption: every claim is cited to
source. Cross-refs: [[Image-Based Lighting]], [[Rendering and GI Roadmap]], [[DynamicDiffuseGI]].

---

## 1. Current state (step 1, done)

Rough specular reflections already exist, sampled from the DDGI irradiance field:

- `DeferredLighting.fs.glsl` computes an `indirectSpecular` term inside the `RENDER_MODULATE_INDIRECT`
  branch: `F0 = mix(0.04, albedo, metallic)` (line 502), a roughness-aware Fresnel `F` (line 503),
  then `R = reflect(-V, N)` and `prefilteredRadiance = getIrradiance(fragPos, R, V, volume) / PI`
  (lines 512-513), giving `indirectSpecular = prefilteredRadiance * F * ao` (line 514), added into
  the final color at line 523.
- `common/BRDF.glsl:18` `fresnelSchlickRoughness(cosTheta, F0, roughness)` was added for this.
- The `/ PI` converts the field's integrated irradiance (`DDGIGetVolumeIrradiance`,
  `ddgi/IrradianceCommon.glsl:198`, returns irradiance scaled by the hemisphere area) into an
  approximate incident radiance so its magnitude sits next to the diffuse term.

**Ceiling of step 1:** the irradiance field is diffuse/low-frequency (a handful of octahedral
texels per probe), so smooth metals reflect a blurry "marble", and a large smooth reflector
spanning several probe cells shows probe-interpolation and visibility-leak artifacts. This is
correct behaviour for a probe-only reflection, and the reason for step 2.

## 2. What step 2 adds

A **ray-traced reflection compute pass** that traces an actual reflection ray for low-roughness
pixels, shades the hit, and writes the result to a screen-space reflection buffer. The lighting
shader then blends: rough pixels keep the DDGI field, smooth pixels take the traced buffer.

The engine already uses **ray query** (`GL_EXT_ray_query`) inside a compute shader for DDGI
(`ddgi/ProbeTrace.cs.glsl:6`), so the reflection pass is the same mechanism — no ray-tracing
pipeline / SBT needed.

## 3. Reuse map — the hard parts already exist

The reflection pass is `ProbeTrace.cs.glsl` with a different ray origin/direction; the hit-shading
machinery below the ray init is reused almost verbatim.

| Need | Existing source to reuse |
|---|---|
| Ray query vs bindless TLAS | `ProbeTrace.cs.glsl:306` (`rayQueryInitializeEXT` on `topLevelAS[]`, set 3 binding 2 `:23`) |
| Fetch triangle + interpolate vertex attributes | `ProbeTrace.cs.glsl` `fetchTriangleIndices:125`, `fetchVertexAttributes:151`, `interpolateVertexAttributes:222`, `getSurfaceDataForHit:250` |
| Evaluate hit material via compiled graph | `ProbeTrace.cs.glsl` `evalHitSurface:264` |
| Direct light at the hit (ray-traced visibility) | `IrradianceCommon.glsl` `DirectDiffuseLighting:167`, `LightVisibility:28` |
| Indirect (multi-bounce) at the hit | `IrradianceCommon.glsl` `DDGIGetVolumeIrradiance:198` |
| Skybox on ray miss | `ProbeTrace.cs.glsl:400` (`textureLod(gCubemaps[...])`) |
| World-pos reconstruction from depth | `DeferredLighting.fs.glsl:407-409` (`invViewProj`) |
| GBuffer material channels | `GBuffer.fs.glsl:45` `gMaterial = (metallic, roughness, ao, packedShadingModel)`; normal `gNormal:8` |

## 4. The reflection pass (compute shader body)

Per pixel (or per half-res pixel):

1. Read GBuffer depth; background (`depth >= 1`) → write 0, return.
2. Reconstruct worldPos (invViewProj), read normal + material. Compute `V`, `F0`.
3. **Gate:** if `roughness > cutoff` or `F0` negligible → skip (pixel stays on the DDGI field).
4. Build the ray: `origin = worldPos + bias`, `dir = reflect(-V, N)` for near-mirror; for
   glossy-but-below-cutoff, GGX-importance-sample the lobe around `R` (needs an RNG + half-vector
   sampling + a frame index).
5. `rayQueryInitializeEXT` / `rayQueryProceedEXT` against the TLAS.
   - **Hit:** reuse `getSurfaceDataForHit` + `evalHitSurface`, then shade
     `radiance = DirectDiffuseLighting(...) + albedo/PI * DDGIGetVolumeIrradiance(...)` (the
     probe query gives multi-bounce for one lookup).
   - **Miss:** sample skybox.
6. Write `radiance` + `hitDistance`. **HDR, no clamp** — unlike ProbeTrace's
   `clamp(radiance, 0, 1)` at `:393`, specular highlights need the range.

## 5. Output — a screen-space radiance buffer, not a GBuffer

The output is a single screen-aligned image (closest to an SSR result buffer), **not** a set of
material attributes:

- **Format:** RGBA16F. `rgb` = reflected **lit radiance** (already shaded at the hit). `a` =
  **hit distance** (ray length).
- The `hitDistance` is functional, not decorative: the denoiser needs it for reflection-specific
  reprojection (virtual reflected position `= worldPos + reflectDir * hitT`) and for scaling the
  spatial filter radius (near hit → tight, far hit → wide). A second channel for the sample PDF
  is optional if doing full glossy importance sampling.
- **Resolution:** half-res (see §6), upscaled at composite.

## 6. Resolution & temporal reconstruction

Resolution scale is *linear*, cost is *area* (scale²):

| Linear scale | Pixel fraction | Temporal frames to reach native sample density |
|---|---|---|
| 50% | 0.25 (quarter) | ~4 frames |
| **70% (≈1/√2)** | **0.49 (half)** | **~2 frames** |
| 100% | 1.0 | 1 |

"70% resolution" is the setting that shades **half** the pixels. It reconstructs to native in
~2 temporal frames vs ~4 for quarter-res — quarter-res still *works* in one frame, it is just
blurrier; the frame count is the accumulation depth needed to recover full detail.

**Why the shallower option matters for reflections specifically:** reflections ghost badly under
temporal accumulation because the reflected image moves with *parallax*, not with the surface's
motion vector. Deeper accumulation (4 frames) = more smear/lag in motion. So a 2-frame path is
preferred for reflections. `hitDistance` feeds the virtual-reflected-position reprojection that
mitigates this.

**Checkerboard vs resolution-scale** (same ~half pixel budget, different reconstruction):

- **70% resolution scale** → downscale the trace, spatial upscale (± temporal). Simple, softer.
- **Checkerboard** → shade a structured half, flip each frame, reconstruct full-res from two
  frames + motion vectors. Preserves detail, but needs solid motion vectors, disocclusion
  handling, and a real reconstruction filter.

**Recommendation:** v1 = ~70% linear (half-res) + shallow (2-frame) temporal clamp + spatial
denoise. Move to checkerboard only if half-res upscale is too soft, and only once motion vectors
exist (see §8).

## 7. Optimization ladder (best ROI first)

Full-screen-always is the right *first* version (proves the math), not the end state.

1. **Half-res trace + bilateral upscale** — ~4× fewer rays; biggest single win.
2. **Roughness / F0 gating** (§4 step 3) — only smooth reflective pixels trace.
3. **Compaction + indirect dispatch** — a plain `if (needsRay)` branch wastes whole GPU waves
   when reflective pixels are sparse. Classify → append reflective pixels to a tight list →
   `vkCmdDispatchIndirect` over just that list for full wave occupancy.
4. **Temporal accumulation + denoise** — reproject last frame (needs motion vectors), accumulate,
   then a roughness/hitT/normal-guided spatial filter. Makes 1 ray/pixel glossy look clean.
5. **Ray binning/sorting** by direction for BVH coherence — advanced, last.

## 8. Prerequisites / gaps

- **Motion vectors are missing.** `GBuffer.fs.glsl` outputs only `gNormal:8`, `gAlbedoSpec:9`,
  `gMaterial:10` — no velocity/motion target. Temporal denoise and checkerboard both require a
  per-pixel motion vector; this must be added first (extra GBuffer target + previous-frame
  view-proj).
- **BRDF integration LUT** for the split-sum specular weight. Owned by [[Image-Based Lighting]]:
  `Generators/BRDFIntegration.cs.glsl` writes RG16F `(NdotV, roughness) -> (scale, bias)`
  (`:9`, `:88`). Composite uses `F0 * scale + bias` instead of the current `F`-only weight for
  correct specular energy.
- **RNG / blue noise** for glossy importance sampling and temporal jitter (frame index push
  constant + a hash or a blue-noise texture).

## 9. Composite (back in DeferredLighting)

Replace the current field-only `indirectSpecular` (lines 512-514) with a hybrid:

- Sample the (denoised, upscaled) reflection buffer.
- `specular = mix(DDGI_field, reflectionBuffer, sharpWeight)` where `sharpWeight` falls off with
  roughness; the gate (§4) guarantees rough pixels never traced, so they stay on the field.
- Weight by `F0` and the split-sum `(scale, bias)` from the BRDF LUT once wired.

## 10. Build order

1. Add motion-vector GBuffer target (prerequisite for temporal).
2. Naive full-screen RT reflection compute pass (clone ProbeTrace hit path), HDR radiance+hitT
   output, no gate/denoise — prove correctness on the silver sphere.
3. Composite hybrid `mix()` in DeferredLighting; verify sharp reflection replaces the marble.
4. Roughness/F0 gate + half-res (70%) trace + upscale.
5. Temporal (2-frame) + spatial denoise.
6. Compaction + indirect dispatch.
7. Wire the BRDF LUT from [[Image-Based Lighting]] for correct specular energy.
8. (Optional) checkerboard reconstruction; ray binning.
