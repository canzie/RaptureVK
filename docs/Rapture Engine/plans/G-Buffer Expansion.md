# G-Buffer Expansion

**Related: [[OpenPBR and Deferred Materials]], [[Material System Overhaul]], [[GBufferPass]], [[LightingPass]], [[Stochastic Screen-Space Reflections]], [[Ray-Traced Reflections]], [[Rendering and GI Roadmap]]**

The G-buffer is full. This plan grows it once, deliberately, so that OpenPBR shading models,
motion vectors, and temporal techniques all land without a second redesign. Zero-assumption:
every claim about current state is cited to source.

---

## 1. Starting state (verified, pre-expansion)

The layout this plan replaces — kept as the record of what was measured. `GBufferPass.cpp:481-518`
created four targets:

| RT | `TextureFormat` | bpp | Contents | Written at |
|----|-----------------|-----|----------|------------|
| RT0 | `RG16F` | 32 | oct-encoded world normal | `GBuffer.fs.glsl:43` |
| RT1 | `RGBA8` (srgb=true) | 32 | `albedo.rgb`, `a = 1.0` | `GBuffer.fs.glsl:44` |
| RT2 | `RGBA8` (srgb=false) | 32 | metallic, roughness, ao, **shadingModelId** | `GBuffer.fs.glsl:45` |
| Depth | `D24S8` | 32 | depth + stencil | — |

**128 bpp / 16 B per pixel.** RT2 has no free channel: the fourth byte holds the packed shading
model (`ShadingModels.glsl:10`). The "1 free byte" noted in [[OpenPBR and Deferred Materials]] §
G-buffer budget is stale — that byte is spent.

Two further facts that shape this plan:

- **Emission is computed and then dropped.** The graph's `SurfaceData` carries `emission` (vec4)
  and `emissiveStrength` (`generated/SurfaceGraphs.glsl:11-21`, declared at
  `GraphDomain.cpp:182-183`), and DDGI consumes it (`ddgi/ProbeTrace.cs.glsl:365`). But
  `GBuffer.fs.glsl:43-45` writes only three targets and never emission — so emissive surfaces
  contribute to GI but do not glow in the directly-lit image.
- **Adding a channel is a one-line change in the graph compiler.** `s_surfaceOutputFields()`
  (`GraphDomain.cpp:174-186`) is the single list defining the generated struct, the sink node's
  input pins, and the per-field fallbacks; it is shared by both the surface domain
  (`GraphDomain.cpp:202`) and the terrain domain (`GraphDomain.cpp:251`). A new field
  automatically becomes a new sink pin and a new struct member for both.

---

## 2. The organising principle

> **The G-buffer stores the *evaluated surface response*, not the authoring parameters.**

OpenPBR's parameter set is large because it is an *authoring* model — `specular_weight`,
`specular_ior`, and `specular_color` all exist so an artist can reason about a layered stack.
The lighting pass does not need them separately; it needs the numbers the BRDF actually consumes.

So `specular_weight × F0(specular_ior)` collapses into **one** stored byte, computed at G-buffer
write time by the graph. Same for anisotropy rotation (stored relative to a canonical frame, not
as a UV-space angle plus a tangent). This is what keeps the target count sane, and it is why the
rich OpenPBR parameter list does not translate into a rich G-buffer.

The corollary: anything requiring information from *outside the pixel* cannot be collapsed this
way, and that is exactly the set of things deferred cannot do (§4).

---

## 3. OpenPBR's layer stack, and what deferred can carry

OpenPBR is a layered BSDF, outermost first. Per layer, what it costs us:

| Layer | Key params | Deferred? | Cost |
|---|---|---|---|
| **Coat** | weight, roughness, ior, color, **own normal** | 🟡 partial | 3 custom bytes; own normal needs a whole extra target |
| **Fuzz / sheen** | weight, roughness, color | ✅ | 3 custom bytes (mono color) |
| **Metal** | base_color→F0, specular_color→F82 | ✅ | already have metallic + base_color |
| **Specular (dielectric)** | weight, ior, color, roughness, anisotropy | ✅ | collapses to 1 byte F0 + 2 aniso bytes |
| **Thin film** | thickness, ior | ✅ | 2 custom bytes |
| **Subsurface** | weight, color, radius, scatter aniso | ❌ *(needs neighbourhood)* | 4 custom bytes **+ split diffuse/specular lighting targets** |
| **Transmission** | weight, color, depth, dispersion | ❌ *(needs what's behind)* | forward or RT only |
| **Emission** | luminance, color | ✅ | 1 byte, see §5 |
| **Opacity (blended)** | geometry_opacity | ❌ | forward pass, ordered blending |

**Definitions, since these get muddled:**

- **Coat** — a thin transparent dielectric film *on top of* the base. Car paint, varnished wood,
  soda cans, wet asphalt. You see the base through it, tinted by `coat_color`, plus the coat's
  own specular highlight. It needs its **own normal** because the clear layer can be glass-smooth
  over a bumpy base — that extra normal is the entire reason coat is expensive.
- **Fuzz / sheen** — a retroreflective microfibre lobe at grazing angles. Velvet, felt, dusty
  cloth. Cheap: no extra normal, just a weight/roughness/tint.
- **Dielectric** — any non-metal. Its specular is a weak (F0 ≈ 0.04), *uncoloured* reflection
  over a coloured diffuse base. Metals invert this: coloured specular, no diffuse. `metallic`
  is the blend between the two, which is why metallic-roughness works at all.
- **Anisotropy** — the specular lobe stretched along a surface direction. Brushed metal, hair,
  vinyl. Needs a direction, which is the awkward part in deferred (§6).
- **Thin film** — interference colours from a film comparable to the wavelength of light. Soap
  bubbles, oil slicks, heat-tinted steel. Cheap: thickness + ior modulate the Fresnel.
- **Subsurface** — light enters, scatters inside, exits *somewhere else*. Skin, marble, wax,
  leaves. This is a light-transport problem, not a surface problem, which is why it does not fit
  the G-buffer model (see §4).

---

## 4. What deferred fundamentally cannot do

Worth stating plainly so it is never relitigated:

1. **Blended transparency.** The G-buffer holds one surface per pixel; blending needs several,
   ordered. Alpha-*tested* (cutout foliage) is fine — still one opaque surface. Only alpha-blend
   is excluded. Solution is universal across engines: opaque deferred → clustered light list →
   forward pass for blended surfaces.
2. **Refraction / transmission.** Needs the scene behind the surface, which has not been composed
   yet. Forward pass or the RT path.
3. **Subsurface scattering, properly.** The deferred-compatible approximation is *screen-space*
   SSS: render diffuse and specular lighting to **separate** targets, blur the diffuse one with a
   separable Burley profile keyed by a per-pixel profile id, then recombine. That means the
   lighting pass stops being a single-output pass. It is achievable, but it is an architectural
   change to [[LightingPass]], not a G-buffer channel — budget it separately.
4. **Per-pixel varying light lists** without clustering. Not a limit we are hitting yet.

Everything else in OpenPBR is a local BRDF evaluation and *is* deferred-compatible, bounded only
by channels and ALU.

---

## 5. Proposed layout

Four colour targets + depth. **192 bpp / 24 B per pixel** (up from 16 B).

| RT | `TextureFormat` | srgb | bpp | Channels |
|----|-----------------|------|-----|----------|
| RT0 | `RGBA16F` | — | 64 | `xy` = oct-encoded world normal, `zw` = motion vector (NDC delta) |
| RT1 | `RGBA8` | ✅ | 32 | `base_color.rgb`, `a` = packed emissive intensity |
| RT2 | `RGBA8` | ✗ | 32 | `metallic`, `roughness`, `ao`, `specular` |
| RT3 | `RGBA8` | ✗ | 32 | `shadingModelId`, `custom.x`, `custom.y`, `custom.z` |
| Depth | `D24S8` | — | 32 | depth + stencil (unchanged) |

Four attachments is the Vulkan guaranteed minimum, so this layout needs no device capability check
(§9).

Notes on each decision:

- **`metallic` / `roughness` / `ao` stay exactly where they are.** They are the channels every
  shading model and every screen-space technique reads, and metallic-roughness is what the graph
  already outputs (`GraphDomain.cpp:178-181`). No reason to touch them.
- **`specular`** replaces the shading-model byte in RT2. It is the *evaluated* dielectric F0,
  already folded from `specular_weight × F0(specular_ior)` by the graph, stored over `[0, 0.125]`
  (ior 1.0–2.0 covers F0 0–0.111; 1.5 → 0.04 is the default). This one byte replaces three
  OpenPBR authoring parameters, per §2. The graph pin carries **F0 itself** (fallback `0.04`), not
  an authoring `specular` slider — the `×8` / `×0.125` remap is a G-buffer storage detail and lives
  only in `packSpecular` / `unpackSpecular`.
- **`shadingModelId` moves to RT3.x**, keeping `packShadingModel` / `unpackShadingModel`
  (`ShadingModels.glsl:10-17`) unchanged — only the channel it is read from moves.
- **RT3.yzw is the reinterpreted "custom data" slot** (§6). This is the mechanism that makes
  future shading models a new enum value rather than a new G-buffer design. It is written as zero
  until a model needs it: the graph deliberately gains **no** `coatWeight` / `fuzzWeight` /
  `anisotropy` pins up front, because an unused pin is dead code in every generated graph and a
  confusing pin in every material. Those fields land with the model that reads them (§13 step 8).
- **Motion lives in RT0.zw**, half-float: ample range for an NDC delta and enough precision near
  zero, which is where accuracy matters. See §7 — it is written as zero in the first implementation.
- **Emission** gets one byte, not a target. `emissive_intensity = exp2(a * 16.0 - 8.0)` when
  `a > 0` gives a 2⁻⁸..2⁸ HDR range from a UNORM8, with the emission *colour* taken from
  `base_color`. **Limitation:** an object cannot glow a different colour than its albedo. If that
  is ever needed, the escape hatch is a small additive forward pass over emissive meshes after
  lighting — which costs zero G-buffer bandwidth, so it is a strictly better place to spend if
  the need arises. Do not spend a whole target on this up front.

**Cost:** at 1920×1080, 33.2 MB → 49.8 MB per frame-in-flight set. `GBufferPass` allocates one
set per frame in flight (`GBufferPass.cpp:512-517`), so multiply accordingly.

### Rejected alternative

Storing `F0.rgb` + `diffuse_albedo.rgb` directly instead of `base_color` + `metallic` (the
"specular/albedo split" layout) removes the metallic branch and supports tinted dielectric
specular. It costs one more byte and is arguably more correct, but it is a larger change to
the graph output contract and buys little today. Noted, not chosen.

---

## 6. Custom data, keyed by shading model

RT3.yzw means whatever `shadingModelId` says it means. This is UE's trick and it is the whole
reason the layout above is future-proof.

| `shadingModelId` | custom.x | custom.y | custom.z |
|---|---|---|---|
| `SM_UNLIT` | — | — | — |
| `SM_OPENPBR_STANDARD` | anisotropy | aniso rotation | *(free)* |
| `SM_OPENPBR_COAT` | coat_weight | coat_roughness | coat_ior |
| `SM_OPENPBR_FUZZ` | fuzz_weight | fuzz_roughness | fuzz_color (mono) |
| `SM_THIN_FILM` | film_thickness | film_ior | *(free)* |
| `SM_SUBSURFACE` | profile id | radius | weight |

Adding a model later = one enum value + one `case` in the lighting switch + a row in this table.
No format change, no new target, no repack of existing channels.

**Anisotropy without a stored tangent.** We do not store a tangent frame, so the rotation is
stored relative to a deterministic orthonormal basis built from `N` alone (branchless
Frisvad/Duff ONB). The graph projects the mesh tangent into that basis at write time and stores
the resulting angle — consistent with §2, and it means brushed-metal direction still follows UVs
without a tangent target.

**Coat's own normal is the one thing this does not solve.** Options, in order of preference:
1. Reuse the base normal for the coat. Correct whenever the coat is smooth and the base bump is
   subtle, which covers car paint and varnish well. **Start here.**
2. Add a sixth `RG16F` target for the coat normal, only once (1) is demonstrably not enough.

Do not add the sixth target speculatively.

---

## 7. Motion vectors

Required by [[Stochastic Screen-Space Reflections]], any temporal denoiser, and TAA. Nothing in
the tree produces them today (confirmed: no velocity output in `GBuffer.fs.glsl:8-10`, no jitter,
no previous-frame matrices in `GPUDataStructs.h`).

**Two implementations, and the cheap one goes first.**

**(a) Camera-only, derived from depth — no G-buffer target at all.** Reconstruct world position
from depth exactly as `DeferredLighting.fs.glsl:417-420` already does, project it with the
*previous* frame's view-projection, and take the NDC delta. This is **exactly correct for all
static geometry** and costs one extra `mat4` in `CameraGPUData` (`GPUDataStructs.h:32-36`) plus a
few ALU. It is wrong only for objects that moved.

**(b) Per-object, written to RT0.zw.** Needs `prevModelMatrix` alongside `modelMatrix` in
`MeshGPUData` (`GPUDataStructs.h:12-13`), updated once per frame, and the G-buffer vertex shader
passing both clip positions to the fragment shader.

**Recommendation:** reserve RT0.zw in the layout now, but implement **(a)** first and land **(b)**
when moving objects actually matter. The scenes are near-static today and (a) unblocks all the
temporal work at a fraction of the cost. Because the layout reserves the channels, switching from (a)
to (b) changes no other channel and no format.

**Jitter:** temporal accumulation wants a sub-pixel jittered projection matrix (Halton 2,3). This
belongs in the camera update, not the G-buffer, but it must land together with motion vectors or
the reprojection will be biased.

---

## 8. Prerequisite: split tonemapping out of the lighting pass

Not a G-buffer change, but it blocks everything downstream and it is cheapest to do in the same
sweep.

`DeferredLighting.fs.glsl:568-570` applies exposure, ACES, and `LinearToSRGB` **inside** the
lighting pass:

```glsl
color *= exposure(1.0);
color = ACESFilm(color);
color = LinearToSRGB(color);
```

So the scene colour buffer is display-referred, and the skybox (`SkyboxPass`) and instanced
shapes passes then draw into an already-tonemapped buffer. Any technique that samples scene
colour as *radiance* — SSR, bloom, exposure metering — is invalid against this.

Worse, in `SWAPCHAIN` mode there is no HDR intermediate at all: `SceneRenderTarget` wraps the
swapchain images directly (`DeferredRenderer.cpp:219`), whereas `OFFSCREEN` mode already allocates
`RGBA16F` (`DeferredRenderer.cpp:214`).

**Required change:** an HDR linear scene colour target in **both** modes, with tonemap + sRGB
moved to a new final composite pass that writes to the swapchain / offscreen target. Skybox and
instanced shapes move onto the HDR target too.

---

## 9. History and derived resources

Owned by this plan because they are frame-lifetime resources, consumed by
[[Stochastic Screen-Space Reflections]].

| Resource | Format | Notes |
|---|---|---|
| `sceneColorHDR` | `RGBA16F` | linear radiance, the output of §8. `R11G11B10F` is the later optimisation (no alpha needed) |
| `historyColor` + mips | `RGBA16F` | previous frame's `sceneColorHDR`, **with a mip chain** for filtered importance sampling. Needs a downsample compute pass |
| `historyDepth` | `R32F` | previous frame's *linear* view depth, for disocclusion rejection |
| `hiZ` pyramid | `R32F` | min-Z pyramid over linear view depth, for the SSR hierarchical trace |
| `ssrHistory` | `RGBA16F` | previous frame's resolved SSR, pre-FG, for temporal accumulation |

### Format gap

**`TextureFormat` has no `R32F`.** The enum (`TextureCommon.h:29-51`) offers `R16F`, `RG16F`,
`RGBA32F`, `RGB32F` — no single-channel 32-bit float. `R16F` is not good enough for a linear-depth
Hi-Z pyramid (at a 1000-unit far plane, half-float resolves to ~0.5 units out there, which will
produce false hits in the trace). **Add `R32F` (`VK_FORMAT_R32_SFLOAT`) to the enum and to
`toVkFormat` (`TextureCommon.h:129`)** as part of this work.

### Colour attachment limit

Five colour attachments exceeds the Vulkan *specification minimum*: `maxColorAttachments` and
`maxFragmentOutputAttachments` are both guaranteed to be at least 4, not 8.

**Nothing is checked today, deliberately.** The implemented layout is **4 attachments** — RT0..RT3 —
because RT4 is reserved but not *written* until per-object motion vectors land (§7b); v1 derives
motion from depth. The whole SSR prerequisite chain runs at 4, inside the guaranteed minimum. A
device check for a fifth attachment that no shader writes only creates a trap that rejects hardware
which would have run fine.

**Current direction: pack into 4.** Normal (`RG16F`, 32 bits) and motion (`RG16F`, 32 bits) pack
exactly into one `RGBA16F`, so the renderer stays inside the guaranteed minimum and needs no device
check:

| | 5-target | **packed 4-target (chosen)** |
|---|---|---|
| RT0 | `RG16F` normal | `RGBA16F` normal.xy + motion.zw |
| RT1..RT3 | unchanged | unchanged |
| RT4 | `RG16F` motion | — |
| colour bpp | 160 | 160 |

Identical total bandwidth and identical memory. The costs, stated so they are not rediscovered:

- **Reads that want only the normal pull 8 B/px instead of 4.** The lighting pass is the main one:
  8.3 MB → 16.6 MB per full-screen read at 1080p, order 0.03 ms. Motion is read only by temporal
  passes, which want the normal anyway.
- **`RT0.zw` is written as zero until §7(b) lands**, costing ~8.3 MB/frame of writes for a channel
  nothing reads yet. Accepted: the alternative is widening RT0 later, and the point of this plan is
  to grow the G-buffer once.
- Motion cannot be viewed as its own image in a frame debugger, so a debug view mode in the lighting
  pass is the way to inspect it.

Note the depth attachment is a separate slot (`pDepthAttachment` in `VkRenderingInfo`) and never
counts against `maxColorAttachments`. A depth prepass (§10) is not a lever on this limit.

---

## 10. Depth prepass — considered, deferred

Not adopted now, recorded so it is not rediscovered from scratch.

**It does not help the attachment limit** (§9) — depth is its own slot. The argument for it is
purely fragment cost: the G-buffer shader is about to go from 3 targets and a simple write to 5
targets plus a full `evalSurfaceGraph` call (the glTF graph is ~5 texture fetches and a pile of
ALU, `generated/SurfaceGraphs.glsl:34-58`). Overdraw that was cheap before is not cheap after.
MDI batches by buffer arena, not by depth (`GBufferPass.cpp:255-264`), so draw order does nothing
to control overdraw — the pass relies entirely on hardware early-Z over unsorted geometry.

It would also produce depth early, which both the SSR Hi-Z pyramid and any future GPU occlusion
culling want anyway.

**Costs and caveats specific to this codebase:**

- Doubles vertex processing and draw submission.
- The terrain pipeline has its own vertex shader (`GBufferPass::createTerrainPipeline`) and would
  need a matching prepass variant.
- Alpha-tested materials need their fragment shader in the prepass too, for `discard`, or the two
  passes disagree on depth.
- The main pass then runs `VK_COMPARE_OP_EQUAL` with depth writes off, which requires bit-identical
  position computation in both passes.

**Decision:** revisit when profiling shows G-buffer fragment cost dominating, or when GPU
occlusion culling lands and wants prepass depth regardless. Do not add it on theory — the vertex
cost is paid unconditionally, the overdraw saving is scene-dependent.

---

## 11. Pass architecture (prerequisite) — **DONE**

Landed and wired: `RenderPass.h` / `.cpp` and `RenderPassContext.h` in `Engine/src/renderer/passes/`.
`GBufferPass`, `LightingPass`, `SkyboxPass` and the new `CompositePass` all inherit it.
`InstancedShapesPass` and `StencilBorderPass` are commented out in `DeferredRenderer` and still need
converting.

**Why this went before the expansion:** the expansion touches every pass's attachment setup, barrier
list and inheritance struct. Against the old copy-paste that edit gets made five times, and again for
each new SSR pass.

The three parts:

- **`RenderPassContext`** — plain per-frame data (scene, camera, render target, targets, settings,
  plus `frameInFlight` / `imageIndex`). Replaces the divergent positional argument lists, where the
  same six values appear in a different order in every pass. `frameInFlight` is
  `Application::getFrameInFlightIndex()` — it selects per-frame-in-flight *resources* and wraps.
  Temporal passes and the Halton sequence need a **monotonic** frame counter, which does not exist
  yet; add it to `Application` alongside `m_frameInFlightIndex` when §13 step 6 lands. Do not
  conflate the two.
- **`RenderPassTargets`** — the textures passes hand one another. `LightingPass` reads
  `context.targets->gbufferNormal` instead of holding a `GBufferPass *`, so passes stop depending on
  each other's concrete types.
- **`RenderPass`** — base class owning the envelope: attachments to `VkRenderingInfo`, attachment
  barriers, `SecondaryBufferInheritance`, `beginRendering` / `endRendering`. A pass implements
  `updateAttachments`, `record`, and `onResize`.

**Ordering stays with the renderer.** There is deliberately no executor and no frame graph; the
renderer keeps calling `beginRendering` / `executeSecondary` / `endRendering` in explicit sequence.
Recording stays parallel — `record` is called for every pass on jobs before any is replayed.

What the conversion established, and what to carry forward:

- `getAttachments` caches on `context.frameInFlight`. A pass that recreates its attachment textures in
  `onResize` **must** call `invalidateAttachments()`, or the first frame after a resize renders into
  freed images. This is the one sharp edge the caching introduced.
- **A pass must not hold "current frame" state that the renderer reads.** This caused the one real
  regression of the conversion: `DeferredRenderer::buildPassContext` runs on the main thread *before*
  the record jobs, but `GBufferPass`'s texture getters indexed an `m_currentFrame` only assigned
  inside `record()`. `RenderPassTargets` therefore pointed at the **previous** frame's G-buffer, and
  the lighting pass reconstructed world position from a one-frame-stale depth buffer — terrain
  shadows flickered under camera motion and looked correct when still. It survived unnoticed before
  the conversion only because `LightingPass` read those getters from inside its own job, racing the
  G-buffer job and usually winning. **Cross-pass getters take the frame index as a parameter.**
- `beginRendering` only barriers `CLEAR` / `DONT_CARE` attachments; `LOAD` attachments are assumed
  already in the correct layout. Retired by Texture layout tracking (TODO at `Texture.h`, deferred
  because it touches every transition call site in the engine).
- `Texture::getFormat()` was fixed to honour `m_spec.srgb` — it defaulted to `true`, so non-sRGB
  `RGBA8` targets such as RT2 reported `R8G8B8A8_SRGB` while the image was `UNORM`. Harmless before,
  because only depth textures queried it; wrong the moment `getInheritance` queries colour targets.
- No `ComputePass` sibling yet. Add it when the Hi-Z pass lands and its barrier shape is known from
  a real caller.
- `endRendering` is only `vkCmdEndRendering`. `GBufferPass` overrides it to also transition its
  targets to `SHADER_READ_ONLY_OPTIMAL` so the lighting pass can sample them. Promote that into a
  declared "readable after" flag on `RenderPassAttachment` only if a second pass needs it.
- A pass whose attachments are all `LOAD` gets no barriers from the base (see above), so `SkyboxPass`
  overrides `beginRendering` to issue its own before delegating.
- `CompositePass` declares **no** attachments: its target is a raw swapchain `VkImage`, not a
  `Texture`. It overrides `getInheritance` / `beginRendering` / `endRendering` wholesale. That is the
  intended escape hatch for anything the `Texture *`-based attachment model cannot express.

---

## 12. Touch list

| File | Change |
|---|---|
| `Engine/src/textures/TextureCommon.h` | ✅ add `R32F` to the enum, `toVkFormat` and the bytes-per-pixel switch |
| `Engine/src/renderer/passes/*Pass.h/.cpp` (all five) | inherit `RenderPass`; `updateAttachments` replaces `getFramebufferSpecification`; `record` replaces `recordSecondary`; delete each pass's `beginDynamicRendering` / `endDynamicRendering` / `setupDynamicRenderingMemoryBarriers` |
| `Engine/src/renderer/DeferredRenderer.cpp:373-440` | build one `RenderPassContext`; drop the four hand-built `SecondaryBufferInheritance` blocks; keep the explicit `executeSecondary` order |
| `Engine/src/renderer/passes/LightingPass.h:56` | drop `GBufferPass *m_gBufferPass`, read `context.targets` instead |
| `Engine/src/materials/graph/GraphDomain.cpp:174-186` | ✅ add `specular` to `s_surfaceOutputFields()` — new sink pin and struct field follow automatically, for both surface and terrain domains. Coat / fuzz / anisotropy fields land with their models (§13 step 8) |
| `Engine/assets/shaders/glsl/generated/*.glsl` | ✅ regenerated on startup, always rewritten; bump `MATERIAL_GRAPH_COMPILER_VERSION` when the emitted contract changes |
| `Engine/assets/shaders/glsl/common/ShadingModels.glsl` | ✅ `packSpecular` / `packEmissive` and their inverses; new `SM_*` ids when models land |
| `Engine/assets/shaders/glsl/common/GBufferOutput.glsl` | ✅ **new** — owns the attachment declarations, `GBufferSurface`, and `writeGBuffer`. The one place the layout is defined |
| `Engine/assets/shaders/glsl/GBuffer.fs.glsl`, `terrain/terrain_gbuffer.fs.glsl` | ✅ fill a `GBufferSurface` and call `writeGBuffer`; their output blocks were byte-identical duplicates |
| `Engine/src/renderer/passes/GBufferPass.cpp` | ✅ RT0 widened to `RGBA16F`, RT3 created; `getFramebufferSpecification`, blend attachments (both pipelines), bindless indices, `updateAttachments`, and the readable-layout barriers all go to 4 |
| `Engine/src/renderer/passes/GBufferPass.h` | ✅ `getShadingModelTexture(frameInFlight)` + its bindless index |
| `Engine/src/renderer/GPUDataStructs.h:32-36` | `prevViewProj` in `CameraGPUData`; `prevModelMatrix` in `MeshGPUData` when §7(b) lands |
| `Engine/assets/shaders/glsl/DeferredLighting.fs.glsl` | ✅ read shading model from RT3, `specular` from RT2.a, emissive from RT1.a; `switch` on the model when a second one exists; tonemap block removed |
| `Engine/src/renderer/passes/LightingPass.cpp` | ✅ `GBufferShadingModelHandle`; the `vec4`s moved to the head of the push constant block so the trailing `uint` run needs no alignment padding — it is now exactly 128 B, the guaranteed limit |
| `Engine/src/renderer/passes/` | ✅ composite/tonemap pass; HDR target in both render-target modes |
| `Engine/src/render_targets/SceneRenderTarget.h/.cpp` | ✅ HDR intermediate for `SWAPCHAIN` mode |

---

## 13. Build order

1. ✅ **`R32F` format + `maxColorAttachments` check.** Trivial, unblocks the rest.
2. ✅ **HDR split** (§8): linear scene target in both modes, new composite/tonemap pass. Verify the
   image is unchanged. This is independently valuable — it is also the bloom/exposure prerequisite.

   **This must precede the pass conversion.** `RenderPassAttachment` holds a `Texture *`, but
   `SceneRenderTarget` owns no textures in `SWAPCHAIN` mode — it hands out raw swapchain
   `VkImage`/`VkImageView` (`SceneRenderTarget.cpp:102-105, 119-122`), so `LightingPass` and
   `SkyboxPass` cannot declare their colour attachment there. After the split they render into an
   HDR `Texture` instead, and only the composite pass touches a swapchain image — that one pass
   overrides `beginRendering`/`endRendering` itself rather than declaring attachments.
3. ✅ **Convert the existing passes to `RenderPass`** (§11). No behaviour change — the image must be
   pixel-identical afterwards. Do this before anything touches G-buffer channels, so the attachment
   edits below are made once instead of per pass.
   *(`InstancedShapesPass` / `StencilBorderPass` still outstanding, commented out of the renderer.)*
4. ✅ **Layout expansion** (§5): RT3 lands, `shadingModelId` moves out of RT2, `specular` takes its
   place, emissive byte in RT1.a, RT0 widened to `RGBA16F` with motion reserved in `.zw`. Graph
   domain field + regenerate. Lighting reads the new locations. Verify no visual change beyond
   emissive surfaces now glowing.

   All G-buffer packing moves into `common/GBufferOutput.glsl`, which declares the outputs and a
   `GBufferSurface` struct written by `writeGBuffer`. `GBuffer.fs.glsl` and
   `terrain/terrain_gbuffer.fs.glsl` had byte-identical output blocks; after this there is one copy
   of the layout, so step 6 and step 8 each edit a single file.

   **Three traps, all hit during this step:**

   - **A new channel is two edits, and the sink pin must be *appended*.** `s_surfaceOutputFields()`
     defines the generated struct; the `SURFACE_OUTPUT` node in `NodeRegistry.cpp` defines the sink
     pins. Both are required — the domain validator asserts on a field with no matching pin. The two
     are matched **by name** (`s_fieldPin`, `MaterialGraphCompiler.cpp:485`), so their orders are
     independent, which matters because `GraphNode.connections` address pins by **index**
     (`{fromNode, fromPin, toNode, toPin}`, e.g. `{18, 0, 20, 5}` in the glTF graph). Inserting a pin
     mid-list silently rewires every existing graph — emission would land in the new float pin.
     Always append.

   - **Adding a graph field cannot bootstrap itself.** `writeGeneratedFiles` is called from
     `TestLayer.cpp:281` at scene setup, but `GBufferPass` compiles its pipelines in its constructor,
     long before that. So the first run after a field is added compiles against the *committed*
     `generated/*.glsl`, fails with `no such field in structure 'surf'`, and crashes on the null
     pipeline — it never reaches the regeneration that would fix it. Hand-edit the committed
     generated files to match what the emitter will produce (struct field, one assignment per graph
     function in field order, one in the dispatcher fallback, `@version` bump), or move generation
     ahead of pipeline creation.
   - **Parameters in included .glsl need a leading underscore.** `float specular` added to
     `evalStandardBRDF` collided with its own local `vec3 specular = D * Vis * F;`. GLSL has no
     namespaces; shared headers use `_specular`, `_packed`, `_id`.
5. **Shading-model switch** in the lighting pass with `SM_UNLIT` + `SM_OPENPBR_STANDARD` only —
   proves the dispatch works before any new model exists.
6. **Camera-only motion vectors** (§7a) + camera jitter. No RT4 write yet.
7. **History resources** (§9): `historyColor` + mip chain, `historyDepth`. This is the handoff
   point to [[Stochastic Screen-Space Reflections]].
8. *(later, demand-driven)* per-object motion vectors into RT4; coat / fuzz / anisotropy models;
   split diffuse/specular lighting targets for SSS.

Steps 1–7 are the prerequisite set for SSR. Step 8 is open-ended and should be driven by an actual
asset that needs it.
