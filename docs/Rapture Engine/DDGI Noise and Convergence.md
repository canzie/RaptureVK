# DDGI Noise and Convergence

A focused diagnosis of the two DDGI artifacts seen in the Sponza benchmark (the "disco" instability that never converges, and the pulsing/growing shadow), why they are intrinsic to the technique, and the concrete options to reduce vs. hide them. Drills into the GI section of [[Rendering and GI Roadmap]]. Cross-refs: [[SceneRenderData]].

---

## 1. The two symptoms

1. **Disco / non-convergence.** The indirect-only center floor (and any pure-dark-to-lit boundary) shimmers like light on water and never settles, *even in a fully static scene*. Worst exactly where variance is highest: penumbra edges, indirect-only regions.
2. **Pulsing shadow.** On the shadowed side, a dark region grows toward the center in pulses, darkening as it grows, occasionally snapping to the correct straight shadow for a split second, then overshooting again.

These are **two different mechanisms** and must not be conflated.

---

## 2. Symptom 1 (disco) is the stochastic residual — intrinsic

DDGI = a noisy Monte-Carlo irradiance estimate per frame (fresh random ray rotation every frame, `DynamicDiffuseGI.cpp` ~232-273) accumulated with an exponential moving average (hysteresis blend, `ProbeBlending.cs.glsl`).

An EMA of independent noisy samples **does not converge to a fixed value** — it converges to a stable *mean* with a permanent residual jitter:

```
residual_std ≈ singleFrameNoise × sqrt((1 - h) / (1 + h))
```

| hysteresis h | residual (fraction of single-frame noise) |
|--------------|-------------------------------------------|
| 0.97 (current) | ~0.123 |
| 0.98 | ~0.100 |
| 0.99 | ~0.0709 |
| 0.995 | ~0.050 |

So **~12% of the per-frame ray noise stays forever** at the current h=0.97. That is the disco. It is governed by the fundamental trade-off triangle of progressive stochastic estimation:

> **Low lag, low residual noise, low ray count — pick two.** You cannot have all three.

This is not a bug in the port or in RTXGI. The original Majercik 2019 paper and the RTXGI SDK both live with it; the 2021 "Scaling Probe-Based Real-Time Dynamic GI" paper added *probe variability* precisely because the noise can't be removed, only detected.

### Ruled out
- The adaptive hysteresis-collapse in the blend shader (`if maxComponent > 0.5: hysteresis -= 0.5`) was suspected, but commenting it out changed nothing — the gamma-encoded deltas rarely exceed 0.5, so it wasn't firing. The disco is the baseline EMA residual, not the adaptive path.

---

## 3. Symptom 2 (pulsing shadow) is feedback under-damping — separate

The trace shader computes each ray's radiance partly from the **previous frame's** probe irradiance (`ProbeTrace.cs.glsl` ~408-420, recursive `DDGIGetVolumeIrradiance`). Information propagates one probe-hop per frame, so a darkening marches across the grid = visible growth.

It *pulses* (overshoots both directions, passing through the correct answer) because the loop is **under-damped**:
- Per-bounce loop gain ≈ albedo, clamped to 0.9 (`ProbeTrace.cs.glsl`, `maxAlbedo`) — close to 1.0, so feedback barely attenuates per bounce.
- High gain + insufficient temporal damping = an underdamped oscillator.

**Fix lever:** lower the bounce gain (`maxAlbedo` 0.9 → ~0.7-0.8). Distinct from the disco fixes.

---

## 4. Probe variability: what it actually does (and doesn't)

RTXGI's convergence feature. Source: `ProbeBlendingCS.hlsl:552-561` (per-texel CoV) + `ReductionCS.hlsl` (reduction to one scalar) + `DDGI_VK.cpp:1631-1640` (the gate).

- **Per-texel coefficient of variation** computed during blend: `CoV = sqrt(luminanceSigma2) / luminanceMean`, where `sigma2 = (sample - oldMean) * (sample - newMean)`.
- **Reduced** (two-stage wave-cooperative reduction) to a single scalar per volume, read back to CPU.
- The app marks a volume **converged** when `avg < threshold` for >16 samples, and then **stops updating it** (excludes it from the update list). Resets on light/geometry/volume change.

**Critical caveat — it is a measurement, not a denoiser:**
- It does **not** reduce the error. It **stops the error from moving**: in a static scene, once converged, trace+blend stop dispatching → the atlas freezes → the dancing stops. It converts flicker into a fixed offset.
- The frozen value is the EMA accumulator *at the freeze frame*, which still carries the ~12% residual. So **it can freeze on a "bad" (within ±12%) snapshot.** That error is **zero-mean** (unbiased — expected value is correct), so it reads as static low-amplitude speckle, which the eye and the bilinear 8-probe interpolation tolerate far better than flicker.
- Mitigation: raise hysteresis / ray count *before* letting it freeze, so the locked-in snapshot error is small.
- It also pays for itself as a **pure perf win** (stop relighting converged volumes) independent of quality.

### Resources it requires
- `ProbeVariability` — `Texture2DArray`, same interior-texel layout as the irradiance atlas, single channel (R16F/R32F). Written by blend shader. (The hook already exists: `ProbeBlending.cs.glsl:239` computes `irradianceSample` "for use in probe variability" but never writes it.)
- `ProbeVariabilityAverage` — smaller `Texture2DArray`, R32G32F (value + weight), shrunk by the reduction passes.
- A host-visible **readback buffer** (1-2 frames stale is fine).
- CPU state: per-volume `samplesSinceReset` counter, `variabilityThreshold` (~0.05), reset-on-change.

### Simpler route for our scale
Grid is ~22³ probes with 6×6 interior irradiance texels — small enough to reduce in a single naive compute pass (or atomic-add into one buffer) instead of RTXGI's two-stage wave-cooperative reduction. Loses nothing but raw efficiency.

---

## 5. The real "own route" options (reduce, not hide)

Variability only freezes. To actually *lower the noise amplitude* (which also makes any frozen snapshot closer to truth), ranked by value:

1. **Spatial filtering of the probe atlas.** Irradiance is a cosine-convolved hemisphere integral — extremely low-frequency. Blur across the octahedral map / neighboring probes aggressively with near-zero detail loss; directly attacks per-texel variance. Highest-value divergence from RTXGI; RTXGI doesn't really do this. **Cheapest high-impact experiment** — improves the live image *and* any future frozen snapshot.
2. **Blue-noise / stratified ray directions** instead of pure random rotation of the Fibonacci sphere. Same variance, but spatially decorrelated so the bilinear filter and the eye reject it. Big perceptual win, cheap.
3. **Screen-space denoise on the final indirect.** There is currently no SSAO/denoise at all. A light spatio-temporal filter on the indirect buffer crushes visible residual regardless of probe noise. This is where most production engines actually win — in screen space, not probe space. (See SSAO/SSGI in [[Rendering and GI Roadmap]] §6.2/§6.4.)
4. **Higher hysteresis** (0.97 → 0.99: 12% → 7%) and **more rays** (∝ 1/√N) — brute force, both cost lag / frame budget.

---

## 6. Recommendation

Not "variability *or* own route" — **layer them**:
1. (optional, ~5 lines) **Manual freeze test**: skip trace+blend after N frames in a static scene. If the disco freezes, it confirms the diagnosis and that variability will work, before building any infrastructure.
2. **Probe-space spatial filter (§5.1) + blue noise (§5.2)** to shrink amplitude → quieter field, closer frozen snapshots.
3. **Probe variability + freeze** to remove residual temporal motion in static scenes (and the perf win).
4. **Lower `maxAlbedo`** to damp the separate pulsing-shadow feedback loop.
5. Long-term, **screen-space denoise / SSGI** masks whatever residual remains and adds the high-frequency detail DDGI structurally cannot.

Bottom line: the disco is **intrinsic** to progressive stochastic GI — it can be shrunk (rays / hysteresis / blue noise / spatial filter), frozen (variability), or hidden downstream (screen-space denoise), but never deleted by tuning alone.
