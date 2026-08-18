# Rendering and GI Roadmap

A planning + knowledge-capture doc from a deep optimization/architecture session. Covers what was changed, where the renderer stands, the limits of the current GI, and a forward roadmap with options and detours. Cross-refs: [[SceneRenderData]], [[Shadow Maps to SceneRenderData]], [[Multi-Window (Multiview) Architecture]].

---

## 1. Context

Target benchmark scene: Sponza + DDGI. Hardware: RTX 4070. The renderer is a Vulkan 1.3 deferred renderer (bindless, dynamic rendering, ray tracing available). The trigger for this session was a perceived performance regression at 4K that turned out to be the viewport finally rendering at *native* 4K (it had been rendering small and stretch-upscaled, which also explained earlier blurriness).

Key mental model established this session: **decouple render resolution from output resolution.** Native 4K + real-time GI is a compromise on *every* GPU; the modern pipeline renders at a sensible internal res (1080p–1440p) and reconstructs. Use **1440p internal as the primary local benchmark** (it maps to 4K DLSS-Quality), 1080p as the "4K Performance" floor.

---

## 2. Completed This Session

### G-buffer slimming (done)
- **Removed the RGBA32F world-position target** (16 bytes/px). World position is now reconstructed in the lighting shader from depth + screen UV + an inverse-view-projection matrix.
  - Added `invViewProj` to `CameraGPUData` (computed once on CPU as `inverse(projection * view)` after the Y-flip). All shaders that index the camera SSBO had their `CameraGPUData` struct updated so the array stride matches (`GBuffer.vs`, `terrain_gbuffer.vs`, `StencilBorder.vs`, `InstancedShapes.vs`, `Skybox.vs`, `DeferredLighting.fs`).
  - Reconstruction: `ndc = vec3(uv*2-1, depth)`, `world = invViewProj * vec4(ndc,1)` / w. View-space depth (for cascade splits + fog) = `-(view * world).z`.
- **Octahedral-encoded the G-buffer normal**: RGBA16F → RG16F (`octEncodeNormal`/`octDecodeNormal` in `MaterialCommon.glsl`).
- **Sky early-out in lighting**: `if (depth >= 1.0) return black;` before the light loop / DDGI sampling (skybox covers it afterward — pass order is gbuffer → lighting → skybox).
- Net G-buffer: **36 → 16 bytes/px** written (normal 4 + albedo 4 + material 4 + depth 4); lighting reads 12 + depth instead of 32. ~half a GB/frame of 4K bandwidth removed. Result: roughly +20 fps; shadow pass cost dropped to ~1/3.

### Per-pass GPU markers (done)
Added Tracy GPU zones (`RAPTURE_PROFILE_GPU_SCOPE`) around each pass's replay on the **primary** command buffer, not in the secondary recording, because `TracyVkCtx` query allocation isn't safe across the parallel recording fibers. They live in each renderer's `replay` since the [[Renderer Restructure]].

### Viewport resize fix (done, earlier)
Offscreen viewport target now resizes with the panel (debounced ~0.2 s to avoid startup GPU thrash/crash), and `AmethystLayer` re-registers the bindless texture when the backing `VkImageView` changes (fixes flicker from sampling freed views after a resize).

---

## 3. Current GPU Profile (post-optimization)

Measured at one 4K Sponza framing (markers, not averaged):

| Pass | Sponza | Look away (sky+terrain) | Scales with |
|------|--------|-------------------------|-------------|
| Lighting | ~1.2 ms (zoom-avg ~700 µs) | ~490 µs | screen res (full-screen), DDGI sampling |
| GBuffer | ~700 µs (zoom-avg ~550) | ~390 µs | geometry + overdraw |
| Shadows (sun CSM) | ~390 µs | — | geometry × cascades |
| Skybox | ~5 µs | — | — |
| **DDGI compute** | **~3.4 ms** | ~3.4 ms | **probes × rays (resolution-independent)** |

Takeaways:
- **Lighting dominates the graphics passes** and has a high fixed floor (full-screen + DDGI per pixel).
- **DDGI compute (3.4 ms) is the single biggest cost in the whole frame** and is resolution-independent — so at 1080p it dominates the frame entirely.
- Frustum culling only; no occlusion culling. No depth prepass.

---

## 4. The Shader Optimization Footgun (open)

**All shaders compile with `shaderc_optimization_level_zero`** (`ShaderCompilation.cpp` — both the `NDEBUG` and debug branches are zero). A comment says `shaderc_optimization_level_performance breaks ddgi`, so optimization was disabled globally and never restored.

- This inflates every GPU number. On NVIDIA the penalty is **moderate (~10–30% on heavy shaders)**, not the 2–3× folklore, because the driver re-optimizes SPIR-V into SASS. Still free perf once unblocked.
- The block is a **latent DDGI undefined-behavior bug** that `-O0` hides (uninitialized var, OOB read, evaluation-order/uniformity assumption, or NaN/inf the optimizer reorders around). The real fix is to find that UB, then re-enable optimization globally.
- **Detour/stopgap:** enable `performance` for everything *except* the DDGI shaders (per-shader opt level) to grab the gbuffer/lighting/shadow win now without breaking DDGI.

---

## 5. DDGI: Where It Stands and Its Limits

The DDGI implementation is a faithful RTXGI port (probe trace → blend irradiance/distance → relocation → classification). Defaults: 256 rays/probe, hysteresis 0.97, irradiance encoding gamma 5.0. Classification already culls inactive probes; ray rotation **is** enabled (an earlier code comment claiming it was disabled is stale).

### The artifacts are the technique's limit, not a bug
- DDGI is a **low-frequency diffuse base layer**. It structurally cannot do sharp indirect shadows, contact detail, thin-geometry occlusion, or specular. Leaking and the need for "gamma tricks" are inherent (present even in NVIDIA's RTXGI Sponza).
- **It is never meant to be used alone** — production layers screen-space AO/GI on top for the high-frequency detail. Judging DDGI on contact quality is grading a bass speaker on its treble.

### The instability ("dancing"), diagnosed
Worst in the **indirect-only center** (no direct sun) and **near light/dark boundaries**. Two compounding mechanisms:
1. **Multi-bounce feedback loop.** Probe rays compute radiance partly by sampling *last frame's* probe volume (`ProbeTrace.cs` recursive `DDGIGetVolumeIrradiance`). In direct-lit areas, direct light stabilizes it; in indirect-only areas the probe lights itself from itself every frame with a freshly-rotated noisy ray set → self-sustaining oscillation. "Shadows trying to grow" is the same loop slowly pumping.
2. **Gamma-5 dark amplification.** Irradiance stored as `irr^(1/5)`; the curve's slope near zero is enormous, so tiny absolute noise in dark probes becomes large encoded swings → trips thresholds → dances.
- A third, secondary source: the adaptive **hysteresis collapse** in `ProbeBlending.cs` (`if maxComponent > threshold: hysteresis -= drop`) misfires on rotation noise (can't tell Monte-Carlo noise from a real lighting change), intermittently bypassing the temporal smoothing. Surface-hit radiance is clamped to [0,1]; the **sky-miss path is not** (a bright sky/sun pops under rotation).

### What was tried, and the verdict
- Softened the hysteresis collapse (`threshold 0.25 → 0.5`, `drop 0.75 → 0.5` in `ProbeBlending.cs`). Marginal, not a fix.
- Considered raising hysteresis (0.97 → ~0.985) for more temporal damping — **rejected**: diminishing returns, and we're at the technique's limit.
- **Decision: stop tuning DDGI.** The proper "more accurate" levers (probe variability tracking, per-frame change clamping, more rays, spatial atlas filtering) all trade away responsiveness or the frame budget. The pragmatic path is **SSAO/SSGI on top to mask + add detail**, accept the low-freq instability.

### "probe variability" (the thing intuited)
RTXGI's real feature: a per-probe / whole-volume measure of how much irradiance is still changing frame-to-frame (coefficient of variation). It doesn't *remove* dancing — it tells you which probes are unconverged, so you can freeze settled probes (perf) and drive adaptive logic. Future option if DDGI stays.

**Deep dive:** see [[DDGI Noise and Convergence]] for the convergence math (residual ≈ 12% at h=0.97), why freezing locks in an unbiased ±12% snapshot, the resources probe variability needs, and the ranked "own route" options (probe-atlas spatial filter, blue-noise rays, screen-space denoise).

---

## 6. Forward Roadmap (options + detours)

Suggested order, front-loading shared infrastructure:

```
depth prepass → SSAO(GTAO) → TAA + bloom + auto-exposure/tonemap
   → atmosphere + volumetrics → SSR(+cubemap fallback)
   → reference path tracer → SSGI → RT sun shadows
```

### 6.1 Depth prepass
- Helps **GBuffer overdraw** (each visible pixel runs material/TBN/4-tex shading once; GBuffer uses `depthCompare = EQUAL, depthWrite = OFF`). Modest immediate win for Sponza-level overdraw (~150–250 µs).
- Real value is as an **enabler**: Hi-Z (for SSR/SSAO marching), MSAA-if-ever, SSAO, decals.
- Does **not** help the lighting pass (the bigger cost).

### 6.2 SSAO — use GTAO, not classic SSAO
- Inputs already available: depth (→ view pos), octahedral normal. Output R8 AO, half-res → depth-aware bilateral blur → bilateral upsample.
- **Apply only to the indirect term** (multiply the DDGI `indirectDiffuse`, never direct light; stacks with material `ao`).
- Biggest "grounds the image" win, and **masks DDGI dark-area wobble** as a bonus. Gives a bent normal + visibility cone for later specular occlusion.

### 6.3 Antialiasing — an open decision, not foregone
Honest framing: **TAA is not a godsend** — it blurs, ghosts, smears in motion, and eats fine detail to do its job. **Correctly-implemented MSAA produces genuinely cleaner geometric edges** (no temporal artifacts). The industry's move away from MSAA-deferred is partly sunk-cost/marketing, not pure technical merit — not an argument on its own.

The real axis is **not "MSAA vs TAA"** — it's **what aliasing dominates, and whether there's stochastic noise to denoise**:
- **MSAA** cleanly solves *geometric edge* aliasing, no temporal blur — arguably the best-looking edges. In deferred: MSAA G-buffer + edge-detect + per-sample lighting *only at edge pixels* (interior shaded once). Expensive but doable. Does **not** touch specular/normal-map shading aliasing (→ needs roughness-based specular AA + alpha-to-coverage for foliage) or stochastic GI/reflection noise.
- **TAA** antialiases geometry *and* shading, doubles as the denoiser for SSGI/SSR/RT, and is the upscaling gateway — at the cost of motion artifacts and softness.
- **Deciding factor for this engine:** if we keep stochastic GI (DDGI/SSGI/SSR), we need temporal accumulation *regardless* — so temporal infra (velocity buffer + reprojection) gets built either way, and MSAA becomes a *complement* for crisp edges, not a replacement.
- If edge clarity is a top priority, a **forward+/clustered** renderer makes MSAA cheap and natural — worth weighing against the just-slimmed deferred path.
- Stopgaps: **SMAA/FXAA** (single post pass) for now. Build the **velocity buffer** early regardless (shared by any temporal denoising + motion blur).

### 6.4 SSGI (extension of SSAO)
- Same horizon march as GTAO, but sample the *lit color* along horizons → near-field one-bounce color bleed. Needs last frame's lit color (reprojected) → leans on TAA.
- Complement: **DDGI = far/multi-bounce, SSGI = near-field contact bleed.** Build after SSAO.

### 6.5 SSR + specular
- DDGI gives **zero specular** today. Options:
  - **SSR + cubemap/probe fallback** (cheap path): march reflection ray in screen space (Hi-Z), sample lit color; roughness → cone/blur; **fall back to skybox/probe when the ray misses or leaves screen** (the fallback is what makes it shippable). Runs after lighting (needs lit color).
  - **RT reflections** (accurate path): use the TLAS, low-res + denoise. Ties into the "RT as reference/benchmark" goal.
- **IBL** is the cheap always-on ambient specular and the natural ray-miss fallback here: prefiltered env cube + BRDF split-sum LUT, composes under SSR. See [[Image-Based Lighting]] for the implementation path.

### 6.6 Atmosphere + volumetrics (replace the skybox)
- Hillaire/Bruneton **sky-atmosphere** (precomputed multi-scatter LUT + aerial perspective). Replaces the static skybox and feeds the GI a *real* sun+sky.
- Pair with **froxel volumetric fog / god rays** for atmosphere.

### 6.7 Post-processing stack (highest perceived-quality per hour)
Rough priority:
1. **TAA** (see 6.3).
2. **Bloom** — mip-chain downsample/blur/upsample (COD/Next-gen style).
3. **Auto-exposure / eye adaptation** — luminance histogram → adapt over time (currently fixed `exposure(1.0)`).
4. **Tonemapper** — ACES is fine; **AgX** or Tony McMapface handle bright saturated colors better.
5. **Photometry / physical units** — EV100, lux/nits, physical light intensities; makes exposure/bloom/auto-exposure consistent instead of magic numbers. Do alongside auto-exposure.
6. **Screen-space contact shadows** — short screen-space depth ray for fine contact shadows CSM misses; cheap, grounds objects.
7. **Color grading / LUT** — biggest art lever per line of code.
8. Finishing: motion blur (needs velocity buffer), DOF, vignette, subtle chromatic aberration, film grain.
- "Looks AAA" trifecta: **TAA + bloom + auto-exposure/tonemap**; add atmosphere + volumetrics and it's there.

### 6.8 Reference path tracer (validation oracle)
- Build a **progressive, accumulating** path tracer: accumulate samples across frames while the camera is static → converges to noise-free ground truth in ~1–2 s; reset on camera move.
- Infra already exists (TLAS + ray queries from DDGI). It's a new compute pass + accumulation buffer.
- Purpose: an **oracle** to measure how wrong DDGI / SSGI / radiance cascades are. Not perf-constrained (amortized). Distinct from a *real-time* PT (needs ReSTIR + denoiser + upscaling — much harder; don't conflate).

### 6.9 RT sun shadows (cheap, clean, has the infra)
- CSM = re-render geometry N× (cost grows with geometry, flat in screen res). RT shadow = 1 occlusion ray/pixel vs TLAS (cost grows with screen res, ~flat in geometry; occlusion rays are the cheapest RT op).
- 1-spp RT hard shadow at 1080p–1440p ≈ **CSM-competitive or cheaper** (~0.1–0.4 ms) with better quality (no acne/peter-panning/cascade seams/range limit). **Hard** shadows at 1 ray; soft penumbra needs cone sampling (N rays) or 1 ray + a shadow denoiser (temporal).
- The DDGI probe trace already does sun-visibility rays, so a screen-space RT sun-shadow pass is a small addition. Strong "nice to have" for a single sun. Hybrid option: RT contact + CSM distance.

### 6.10 Radiance Cascades 3D (parked)
- Loved the 2D implementation; world-space 3D probe RC is immature. PoE2 ships RC but in **screen space**, exploiting the near-fixed ARPG camera angle. The 3D-probe frontier lives on the Radiance Cascades Discord (revisit when their demos stabilize). Keep on the experimental shelf, off the critical path. See [[Radiance Cascades Theory]] if/when expanded.
- Separate, cheaper thread to pull: borrow only RC's **level scaling policy** for DDGI probes without adopting RC transport. See [[Cascaded Irradiance Probes]].

---

## 7. Performance Expectations / Philosophy

- **Native 4K + real-time GI is a compromise on every GPU.** Cyberpunk path tracing renders ~1080p internal even on a 4090. The 4070 is above its tier for native 4K GI; no consumer card does it without upscaling.
- **RT performance reality:**
  - Crytek "Neon Noir" = RT *reflections only* (hybrid voxel/screen-space, ran on non-RTX) → cheap, hence high native fps.
  - Single-effect RT (reflections OR AO OR shadows) at ¼–½ res + denoise → native-viable, 100+ fps possible.
  - Full path tracing (Cyberpunk Overdrive) ≈ 10× heavier. With ReSTIR + Ray Reconstruction + SER: 4090 native 4K PT ~15–22 fps; 4090 native 1080p ~55–70 fps; **4070 native 1080p ~25–40 fps**. Designed to ship with DLSS Performance + Frame Gen.
- **Most of the best-looking games shipped with baked GI, not real-time GI.** Lightmaps/Enlighten/PRT are artifact-free, cheap at runtime, gorgeous — they sidestep *every* DDGI problem here. Price: geometry + bounce can't change at runtime. Decima (Horizon/Death Stranding) has GI, but it's **precomputed** with time-of-day interpolation, not Lumen-style real-time. If scenes are static enough, baking is the honest "looks amazing" path; reserve dynamic GI for what actually moves.

### GI technique landscape (reference)
- **Screen-space (SSAO/SSGI/SSR)** — cheap, high-freq, view-dependent, complement only.
- **Probe (DDGI/RTXGI)** — current; low-freq diffuse, leaks, dark instability.
- **Surfels (EA SEED GIBS)** — discs *on surfaces* → less leaking than a grid, but allocation/coverage/popping under motion, world-space hash-grid management, still low-freq diffuse. Trades leaking for management complexity, not a clear win.
- **Voxel cone tracing (VXGI, CryEngine SVOGI)** — diffuse + coarse glossy; resolution leaking/blockiness, heavy memory, hard to scale. Natural fit for voxel worlds.
- **SDF hybrid (UE5 Lumen)** — five techniques in a trenchcoat (screen traces + SDF traces + surface cache + radiance caching + denoise); current shipping SOTA dynamic GI.
- **ReSTIR PT (Cyberpunk PT, Portal RTX)** — most accurate; needs HW RT + denoise + upscaling.
- **Radiance Cascades** — see 6.10.
- **Baked (lightmaps/Enlighten/PRT)** — artifact-free, cheap, static.

---

## 8. Design Philosophy — Comparison-Driven, Swappable Techniques

The guiding principle: this is a **comparison platform**, not a renderer married to one technique. Implement several approaches, A/B them in the real scene, let the image (and the reference path tracer) decide — never lock in.

- **Invest in the shared substrate**, not the techniques. The permanent, reusable layer: G-buffer, depth, **velocity buffer**, history buffers, **TLAS/BLAS**, and the reference PT oracle. This outlives any single technique.
- **Keep techniques as thin, swappable consumers** of that substrate: AA (MSAA / TAA / SMAA), GI (DDGI / SSGI / Radiance Cascades / reference PT), specular (SSR / RT reflections). Implement multiple, compare, stay flexible.
- **Flexibility ≠ two parallel renderers.** The cost lives in the shared substrate; the swappable layer stays thin. Caveat: **AA is not purely a top layer** — TAA vs MSAA leaks into pipeline-wide concerns (jitter, history, per-sample vs single-sample shading). "Both" means *one* common temporal/jitter scaffold with MSAA and TAA as resolve options, not forked pipelines.
- **TLAS/BLAS is the keystone reusable asset** — it feeds DDGI, the reference PT, RT shadows, RT reflections, and RT AO. It survives GI-technique churn, so building it well is never wasted even while DDGI's long-term future is uncertain. If something better than DDGI arrives, it plugs into the same substrate.

## 9. Open Decisions
- Re-enable shader optimization → requires fixing the latent DDGI UB (or per-shader opt-level stopgap).
- TAA scope: full resolve vs. minimal velocity + per-effect temporal.
- Whether scenes are static enough to justify a **baked GI** path for "money shots" alongside dynamic DDGI.
- Amortize DDGI probe updates (round-robin a fraction of probes/frame) — biggest single fps lever still untaken; independent of the instability work.
