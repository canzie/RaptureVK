# OpenPBR and Deferred Materials

**Related: [[Material]], [[MaterialData]], [[MaterialInstance]], [[GBufferPass]], [[LightingPass]]**

Design notes for adopting OpenPBR as the shading model and where it fits (and does not fit) our deferred pipeline. Captured from a design discussion, not yet implemented.

## OpenPBR vs MaterialX (they are different things)

- **OpenPBR** = a *shading model* (layered BSDF spec + parameter set). Adopting it = grow the material struct + rewrite the BRDF. Bounded work.
- **MaterialX** = an *interchange / node-graph format* (XML doc model + standard node library). It is the guide for a future node editor and for import/export, NOT a runtime to embed. OpenPBR is itself expressed as a MaterialX node (`open_pbr_surface`), which is why they feel entangled.
- Do NOT embed MaterialXCore + ShaderGen as the engine runtime: its GLSL codegen emits monolithic per-material shaders that fight our bindless / single-pipeline / MDI design.
- Translation only lowers cleanly in the trivial direction: a MaterialX doc whose surface is a single `standard_surface` / `open_pbr_surface` with constant + plain-texture inputs maps 1:1 onto [[MaterialData]] (the 90% import case: glTF, most Substance exports). A MaterialX graph with real procedural nodes cannot live in `MaterialData` and needs the graph compiler (see `Engine/src/materials/PROCEDURAL_MATERIALS_DESIGN.md`).

## Keep the static struct

The static uber-material path stays. OpenPBR is "the same idea with more constants" which is exactly what the flat 96-byte std140 [[MaterialData]] is good at. Dynamic behavior (noise, texture math) is a *separate* path via the graph compiler (interpreter bytecode UBO for live editing, baked GLSL for shipping). Two coexisting paths, same as UE/Unity. Struct memory is a non-issue (256B x hundreds of materials = nothing); the real costs are G-buffer bandwidth and BRDF ALU.

## OpenPBR work, tiered by the deferred constraint

- **Cheap (days) - "base" OpenPBR:** metallic-roughness maps almost 1:1 onto OpenPBR `base_metalness` + dielectric specular layer. Add scalars (`specular_weight`, `specular_ior`, `specular_color`, split `specular_roughness` from base, `emission_luminance`) and rewrite the GGX eval in `DeferredLighting.fs.glsl` to OpenPBR Fresnel / energy compensation. Struct grows ~96B to ~160B. One bindless array, one pipeline.
- **Medium (weeks) - coat / sheen(fuzz) / anisotropy / thin-film:** each needs extra G-buffer channels; coat needs its own normal. Pushes target count up or routes those materials to the forward pass.
- **Expensive (months / RT-only) - subsurface / transmission / translucency:** do not fit deferred. Natural home is the ray-traced closest-hit path or the forward pass.

## What is "coat"

A clear-coat layer: a thin transparent dielectric film on top of the base material. You see the base through it (attenuated / tinted by `coat_color`), and it adds its own specular highlight. Examples: car paint, varnished wood, soda cans, wet surfaces. Params: `coat_weight`, `coat_roughness`, `coat_ior`, `coat_color`, and crucially its **own** `coat_normal` (the clear layer can be smooth while the base is bumpy). That extra normal is why coat costs a whole G-buffer target.

## G-buffer budget

Current layout (`GBufferPass.cpp`):

| RT | Format | bpp | Contents |
|----|--------|-----|----------|
| RT0 | `R16G16_SFLOAT` | 32 | oct-encoded world normal |
| RT1 | `R8G8B8A8_SRGB` | 32 | base color rgb + a |
| RT2 | `R8G8B8A8_UNORM` | 32 | metallic, roughness, ao, (1 free) |
| Depth | `D24_UNORM_S8_UINT` | 32 | depth + stencil |

Target plan:

- **OpenPBR base (do this): 4 color targets, ~160 bpp.**
  - RT0 `RG16F`: normal (keep)
  - RT1 `RGBA8_SRGB`: base_color.rgb + a
  - RT2 `RGBA8`: metallic, roughness, ao, specular_weight
  - RT3 `RGBA8` (new spec/misc): specular_ior (to F0), specular_roughness, anisotropy, **shading-model ID**
  - Emission: do NOT spend a channel; add it straight into the HDR lighting target (additive, HDR, no lighting needed).
- **Add coat: 5 targets.** Coat needs its own oct normal (`RG16F`) plus coat_roughness/ior.
- **Beyond that** (high-quality sheen + anisotropic tangent + thin-film at once): 6+ targets = fat G-buffer; stop here and use the shading-model ID trick instead.

### Shading-model ID trick (land this from day one)

Store a **shading-model enum per pixel** (one byte in RT3) and *reinterpret* a shared "custom data" target's channels based on it: RT3 = coat params for a coat pixel, sheen params for a sheen pixel. This is what UE5 does (~5-6 targets GBufferA-E + a "custom data" slot reused per shading model, keyed by a shading-model ID). Adding coat/sheen later becomes a new enum value + reinterpreted channels, not a G-buffer redesign.

## Transparency = separate forward pass (universal)

Deferred physically cannot do blended transparency: the G-buffer stores one surface per pixel; blending needs multiple ordered surfaces. So every "deferred" engine is a hybrid:

1. **Opaque + alpha-tested (masked)** to the deferred G-buffer. (Cutout foliage stays deferred - still one opaque surface per pixel. Only alpha-*blended* is the problem.)
2. Build a **clustered light list**.
3. **Alpha-blended / translucent** in a **forward pass** after deferred lighting, back-to-front, reusing that clustered light list. Forward+ / clustered exists precisely to make this affordable (cull lights into screen clusters so the forward shader does not loop every light).

OpenPBR **transmission / translucency** naturally lands in this forward pass (or the RT path). Back-to-front sorting is the cheap default; OIT / weighted-blended is the fallback if sorting artifacts matter later (not worth it now).

## Suggested sequencing

1. OpenPBR-base as a pure shading-model swap on the existing static struct + `DeferredLighting.fs.glsl` BRDF rewrite; add the shading-model ID to RT3.
2. Node editor against MaterialX semantics with interpreter-mode live preview.
3. Baked GLSL for shipping.
4. Defer coat/sheen/SSS/transmission until the fat-G-buffer vs forward vs RT decision is made.
