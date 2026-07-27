# Cascaded Irradiance Probes

Applying the **radiance cascades scaling policy** to [[DDGI]] irradiance probes: several nested probe levels where spatial density drops and ray count rises as you go outward.

> Idea / exploration. Concept only — worth a throwaway 2-level test to see how it looks and performs before committing to anything.

Not the same as implementing radiance cascades. RC is a radiance transport structure; this borrows only its *level scaling policy* and applies it to a cosine-projected irradiance field.

## The policy splits into three claims

RC bundles three things because in RC a ray **is** a directional bin. For irradiance probes they are independent, and they have different verdicts.

| Claim | Verdict for irradiance |
|---|---|
| Spatial probe density decreases outward | **Transfers fully.** The main win. |
| Angular *storage* resolution increases outward | **Does not transfer.** Hard ceiling. |
| Angular *sampling* rate (rays) increases outward | **Transfers**, but for a different reason. |

### Spatial density — transfers

The dense-near/sparse-far justification is **parallax**, not radiance. Two neighbouring probes disagree strongly about what they see when visible geometry is close, and agree almost exactly when it is far. That argument is about the geometry of visibility, so it is indifferent to whether radiance or irradiance is stored.

Stronger for irradiance, if anything: the spatial gradient of irradiance is driven almost entirely by near-field effects (corners, contact darkening, local occluders). The distant contribution is nearly spatially constant and interpolates across large distances without error.

### Angular storage — does not transfer

Irradiance as a function of the normal is radiance convolved with a clamped cosine. That kernel is a severe low-pass — its SH coefficients are effectively zero past band 2 (â₀≈0.886, â₁≈1.023, â₂≈0.495, â₃=0, decaying ~n⁻² after), so irradiance carries roughly **9 meaningful degrees of freedom**. Ramamoorthi–Hanrahan.

Consequence: a distant window or sun disc cannot be resolved as a sharp directional feature in an irradiance field regardless of how many directional bins are allocated. Even a small octahedral map is already oversampled relative to band-2 content. **Angular storage stays fixed and small at every level.**

This is the property being given up. RC needs growing angular resolution because it resolves penumbrae, and penumbra sharpness is exactly what cosine convolution destroys.

Upside hidden here: RC's memory balance is ⅛ probes × 4× directions = ½ per level, total ≈ **2×** the base. Holding angular storage fixed makes each level ⅛ of the previous, total ≈ **1.14×** the base. An irradiance hierarchy is far cheaper in storage than a radiance one — the savings just can't be spent on directional detail.

### Ray count — transfers, re-derived

More rays outward is right, but motivated by **variance**, not feature resolution.

A probe in a tight interior is dominated by one nearby wall: low-variance integrand, converges in a few samples. An outer-level probe integrates a much larger domain containing far more scene diversity — dark interior, sunlit surfaces, sky, and a sun that is tiny in solid angle and enormous in radiance. Textbook high variance.

Nuance: the far field is also the most **temporally stable** part, so those samples can be bought over time rather than per frame. Outer levels want more samples per unit time, not necessarily more rays per frame. Inner levels are the ones that must react fast, and they are the cheap ones.

## Composition between levels

Irradiance is linear in radiance, so partitioning the integration domain by distance and adding shells is **exact**:

`E_total = Σ_shells ∫ L_shell(ω) cosθ dω`

Projection to irradiance does not break additive composition. What breaks it is **occlusion coupling**: if a ray is blocked inside the near shell, the far shell must contribute zero along that direction. A sparse far-level probe sitting elsewhere in space has no knowledge of near-field occluders, so it over-reports. This is a visibility problem, not a projection problem.

Minimum extra information needed to fix it is a **per-direction visibility/transmittance field** attenuating the far term. Visibility statistics are themselves low-frequency — mean distance plus variance of distance per direction is enough for a Chebyshev-style estimate — so this does not reintroduce a demand for high angular resolution. This is what makes additive composition viable rather than falling back to nearest-level-wins with a blend band.

## Performance or visuals?

Primarily a **cost-scaling** change. It is not "same scene, faster" — on a scene that already fits comfortably in one volume it adds overhead and buys nothing. The win appears when range >> near-field detail scale, where a single volume has to choose between coarse spacing everywhere or an unaffordable probe count.

What that scaling gets spent on is where visuals improve:

- **Near-field density.** The innermost level only covers a small radius, so it can be far tighter than a uniform volume could afford. Light leaking is fundamentally a probe-spacing-vs-geometry-thickness problem, so tighter spacing means less leaking, better contact darkening, better small-scale GI. Probably the biggest visual win.
- **Range.** Indirect light where a bounded volume currently provides none.
- **Stability.** Fewer volume-boundary pops as the camera moves.
- **Effective extra bounces.** Coarse levels feeding fine levels gives long-range multi-bounce transport nearly free.

Will *not* help: sharp or small-scale directional features and penumbrae (cosine ceiling), and it introduces a **new** risk — a visible brightening band at level transitions from the occlusion-coupling leak above.

## What a test should look for

Two levels is enough to learn everything interesting. The questions worth answering:

1. Is the transition band between levels visible, and how bad without a visibility-attenuated far term?
2. How tight can the inner level go before cost matters, and how much leaking does that actually remove?
3. Does the outer level converge acceptably on temporal accumulation alone at a low update rate?

Related: [[DDGI]], [[DDGI Noise and Convergence]], [[LightingPass]], [[Rendering and GI Roadmap]]
