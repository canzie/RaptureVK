# Image-Based Lighting

Investigation of the path to adding image-based lighting (IBL): a diffuse irradiance map, a prefiltered specular environment cubemap, and the BRDF split-sum integration LUT, plus how they slot into the existing deferred lighting and DDGI. Zero-assumption: every claim below is cited to source. Cross-refs: [[Rendering and GI Roadmap]], [[MaterialData]], [[SceneRenderData]].

---

## 1. What "IBL" and "the LUT" mean here

The [[Rendering and GI Roadmap]] says "LUT" for three unrelated things. Only one is part of IBL:

| "LUT" in the roadmap | What it is | Part of IBL? |
|---|---|---|
| **BRDF integration LUT** (Karis split-sum) | 2D RG16F table `(NdotV, roughness) -> (scale, bias)` for the specular env term | **Yes, this doc** |
| Atmosphere multi-scatter LUT (roadmap 6.6) | Precomputed sky scattering | No, separate feature |
| Color grading LUT (roadmap 6.7) | 3D art-direction lookup in post | No, post-processing |

So IBL = irradiance cube + prefiltered specular cube + BRDF integration LUT. The split-sum approach (Karis 2013) factors the specular envmap integral into a roughness-mipped prefiltered radiance cube and a scene-independent 2D BRDF LUT.

---

## 2. Current state

- **No IBL exists.** `DeferredLighting.fs.glsl` indirect block (lines 494-510) is DDGI diffuse or, in ambient mode, a flat `vec3(0.03) * albedo` (line 508). DDGI is diffuse-only and gives **zero specular** (roadmap 6.5), so ambient specular is entirely absent today.
- **The skybox is already a real HDR cubemap.** `SkyboxComponent.skyboxTexture` (`components/Components.h:216`) is an `AssetPtr<Texture>`, RGBA16F, ClampToEdge, sampled as `samplerCube` in `Skybox.fs.glsl:12`. It is either loaded from file or procedurally generated (`ProceduralTexture::generateAtmosphereCubemap`, `generators/textures/ProceduralTextures.cpp:572`). This is the IBL source, for free.
- **Bindless already supports cubemaps.** Set 3 / binding 0 is aliased as `sampler2D gTextures[]` in `DeferredLighting.fs.glsl:34` and as `samplerCube u_gTextures[]` in `Skybox.fs.glsl:12` — the same binding, different type alias. So the lighting shader can add a `samplerCube` alias and index IBL cubes by bindless handle with no new descriptor set.
- **The lighting shader is IBL-ready.** It already computes `F0 = mix(vec3(0.04), albedo, metallic)` (line 501) and has N, V, roughness, metallic, ao in hand. Push constants (`LightingPass.cpp:14`) are trivial to extend.
- `BRDF.glsl` has `fresnelSchlick` and F0; needs a roughness-aware Fresnel and the split-sum combine added.
- `generators/textures/CubemapIrradianceGenerator.h` and `HDRIConverter.h` exist but are **empty stubs** (1 comment line / 0 bytes).

---

## 3. The bake pattern (corrected)

**`ProceduralTexture` is the wrong base and stays that way.** It is output-only: writes a storage image at set 4 / binding 0 and takes **no sampled input** (by design, will not change). Irradiance convolution and prefilter both must **sample** the source env cube, so they cannot use it.

**The correct template is `TextureCompressor::encode`** (`generators/textures/TextureCompressor.cpp:150`). It is the existing "sample a source, write a mipped destination" compute path:

- Builds a bespoke set-4 `DescriptorSet` with a `COMBINED_IMAGE_SAMPLER` **sampled source** at `CUSTOM_0` and an output at `CUSTOM_1` (lines 205-221).
- Loads a **precompiled `.spv`** via `new Shader` (lines 56-81) because glslang overflows the job-fiber stack — shaders baked on a worker must be precompiled, `.glsl` is only safe main-thread.
- Per-mip dispatch loop (lines 257-268).
- Yields instead of blocking a worker: `graphicsQueue->addToBatch` + `submitGpuWait` + `jctx.waitFor` (lines 308-315).

For IBL the `CUSTOM_1` output becomes a **storage image** (`imageStore` into the target cube mip) rather than the `StorageBuffer`+copy that the BC path uses.

**Exception:** the **BRDF integration LUT** samples nothing (pure `NdotV`/roughness math), so it is the one bake that genuinely fits `ProceduralTexture`, or it can share the bespoke path for consistency.

---

## 4. The one plumbing gap: per-mip cube storage views

`Texture` creates a single cube storage view (`Texture.cpp:618-622`) that copies `viewInfo` with `levelCount = m_spec.mipLevels` at `baseMipLevel = 0` (lines 608-609). That works for the atmosphere cube (`mipLevels = 1`) but a Vulkan storage-image view must be single-mip. Prefilter writes each roughness mip separately, so it needs one storage view **per mip** (`viewType = 2D_ARRAY`, `layerCount = 6`, `baseMipLevel = i`, `levelCount = 1`).

Fix options: add per-mip storage views to `Texture`, or create transient views inside the prefilter pass. This is the first real code change and is isolated.

---

## 5. Phased path

- **Phase 0 — cube storage views.** Per-mip 2D-array storage views for cubemaps (section 4). Unblocks prefilter; small and isolated.
- **Phase 1 — BRDF LUT.** `BRDFIntegration` compute -> 2D RG16F (~512^2). Scene- and view-independent, bakes **once ever** (could even ship as an asset). Standalone, testable against reference images before any scene wiring.
- **Phase 2 — irradiance convolution.** Sample the skybox cube, cosine-weighted hemisphere integral -> small irradiance cube (~32^2). Bespoke pass per section 3.
- **Phase 3 — prefilter.** GGX importance sampling -> prefiltered specular cube (~128^2, ~6 mips), roughness = mip / maxMip. Uses Phase 0 views, per-mip dispatch.
- **Phase 4 — bake manager + wiring.** A holder (3 textures + bindless handles) rebuilt when the skybox cube changes (natural hook near the skybox handoff, `DeferredRenderer.cpp:289-295`, or the `Environment` system, `components/systems/Environment.h`). Add 4 handles to `LightingPushConstants`.
- **Phase 5 — shader integration.** Add the `samplerCube` alias, `fresnelSchlickRoughness`, and the split-sum combine in the indirect block, gated on a lighting flag like the existing `RENDER_USE_GLOBAL_ILLUMINATION`.

---

## 6. The one real design decision: double-counting vs DDGI

DDGI supplies diffuse indirect but zero specular (roadmap 6.5). IBL must not double-count diffuse:

- **DDGI active:** IBL adds **specular only** (prefiltered cube x BRDF LUT); DDGI keeps diffuse.
- **Ambient mode:** IBL adds **both** — irradiance-based diffuse *replaces* the flat `0.03 * albedo`, plus specular.

Cleanest fit: an `IBLSettings` alternative (or a specular sub-setting) on `IndirectLightingComponent`'s `std::variant` (`components/IndirectLightingComponent.h:37`, currently `{monostate, AmbientSettings, DDGISettings}`), with the shader term gated on a flag. This makes IBL specular the shippable ambient-specular story now, with SSR (roadmap 6.5) layering on top later — they compose, IBL being the ray-miss fallback.

---

## 7. Open questions before coding

- **Bake thread:** main-thread (`.glsl` ok) or job worker (must precompile `.spv`)? Dictates the shader build step.
- **IBL source:** always the skybox cube, or a separately-assignable HDRI? Arbitrary `.hdr` needs equirect->cube first (`HDRIConverter.h` is an empty stub).
- **BRDF LUT:** baked at runtime or shipped as an asset (it never changes).
