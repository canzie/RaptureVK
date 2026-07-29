# Monte Carlo Integration

How you evaluate an integral you cannot solve and cannot afford to grid up. Every sampling-based
technique in this renderer — [[Ambient Occlusion]], screen-space reflections, DDGI probe tracing —
is a Monte Carlo estimator wearing different clothes, so it is worth understanding once, properly.

Prerequisite: [[Solid Angle]].

---

## 1. The problem

We want a number:

$$I = \int_{\Omega} f(\boldsymbol{\omega}) \, d\omega$$

where:

- $I$ — the number we are after.
- $\Omega$ — the hemisphere of directions above a surface point.
- $\boldsymbol{\omega}$ — one direction, a unit vector.
- $f$ — the integrand, whatever the lighting integral puts there.
- $d\omega$ — a differential patch of solid angle (see [[Solid Angle]]).

We cannot solve it in closed form because $f$ contains **visibility** — whether
a ray in direction $\boldsymbol{\omega}$ escapes or hits something — and visibility is a property of
the whole scene, not an expression you can antidifferentiate.

## 2. Why not just use rectangles

The method you are thinking of is a **Riemann sum** (its practical forms are the *rectangle*,
*trapezoid* and *Simpson's* rules, collectively **quadrature**): chop the domain into $N$ regular
cells, evaluate $f$ at each, multiply by cell size, add up. In 1D it is excellent — the trapezoid
rule converges as $O(N^{-2})$, Simpson's as $O(N^{-4})$.

It falls apart here for three reasons, and each one matters independently:

**Dimensionality.** Those convergence rates assume you can afford $N$ points along *each* axis.
A regular grid in $d$ dimensions with $N$ points total has only $N^{1/d}$ points per axis, so the
error rate degrades to $O(N^{-2/d})$. A hemisphere is 2D, which is survivable; but one light bounce
adds two more dimensions, and a path with $b$ bounces lives in $2b$ dimensions. This is the **curse
of dimensionality**. Monte Carlo's error rate, as we will see, does not depend on $d$ at all.

**Discontinuity.** Quadrature's convergence rates are theorems about *smooth* functions — they come
from Taylor expansions and need bounded derivatives. Visibility is a step function with hard edges
along every silhouette in the scene. Across a discontinuity those rates collapse to $O(N^{-1})$ or
worse, and the guarantee evaporates.

**Aliasing.** A regular grid sampling a scene full of regular structures produces *structured*
error: banding, moiré, repeated patterns. Random sampling produces *unstructured* error, which looks
like noise. That sounds worse and is much better — noise is high-frequency and uncorrelated between
neighbouring pixels, so a denoiser can remove it. Banding is a lie that looks like geometry, and no
filter can tell it from the real thing.

## 3. The estimator

Pick a probability density $p(\boldsymbol{\omega})$ over the domain, draw $N$ independent samples
$\boldsymbol{\omega}_1 \dots \boldsymbol{\omega}_N$ from it, and compute

$$\boxed{\;\langle I \rangle_N = \frac{1}{N} \sum_{k=1}^{N} \frac{f(\boldsymbol{\omega}_k)}{p(\boldsymbol{\omega}_k)}\;}$$

where:

- $\langle I \rangle_N$ — the **estimator**: an approximation of $I$ from $N$ samples. The angle
  brackets are the usual notation for "estimate of". It is a random quantity, not a fixed number.
- $N$ — how many samples are drawn.
- $p$ — the **probability density** the samples are drawn from. Must be positive wherever $f$ is
  non-zero, and must integrate to 1 over $\Omega$.
- $\boldsymbol{\omega}_k$ — the $k$-th sampled direction, drawn according to $p$.

That is the entire method. The only non-obvious part is dividing by $p$.

### Why the division by $p$

Because we chose where to look, and we have to undo that choice. If we sample a direction twice as
often as another, its samples must count for half as much, or we would be reporting our own bias
back to ourselves. Dividing by the density does exactly that.

### Proof that it is correct (unbiased)

The estimator is random, so "correct" means its **expected value** is the true integral. Recall the
definition of expectation for a function of a random variable $\boldsymbol{\omega} \sim p$:

$$E[g(\boldsymbol{\omega})] = \int_{\Omega} g(\boldsymbol{\omega})\, p(\boldsymbol{\omega})\, d\omega$$

Apply it with $g = f/p$:

$$E\big[\langle I \rangle_N\big]
= \frac{1}{N}\sum_{k=1}^{N} E\!\left[\frac{f(\boldsymbol{\omega})}{p(\boldsymbol{\omega})}\right]
= E\!\left[\frac{f(\boldsymbol{\omega})}{p(\boldsymbol{\omega})}\right]
= \int_{\Omega} \frac{f(\boldsymbol{\omega})}{p(\boldsymbol{\omega})}\, p(\boldsymbol{\omega})\, d\omega
= \int_{\Omega} f(\boldsymbol{\omega})\, d\omega = I$$

The $p$ cancels. That cancellation *is* Monte Carlo integration — everything else is detail.

Note what this does **not** say: it does not say any single estimate is close to $I$. It says the
estimates are centred on $I$. One sample gives you a wild guess that is right *on average*. Averaging
more of them is what makes it useful, and that is a statement about variance, not bias.

### Sanity check with uniform sampling

Over the hemisphere, uniform means $p = 1/(2\pi)$ (constant, and integrating to 1 over $2\pi$ sr).
Then

$$\langle I \rangle_N = \frac{1}{N}\sum_k \frac{f(\boldsymbol{\omega}_k)}{1/(2\pi)} = \frac{2\pi}{N}\sum_k f(\boldsymbol{\omega}_k)$$

which is "average value of $f$ $\times$ area of the domain" — the thing you would have guessed. The
general estimator is that idea, generalised to non-uniform sampling.

## 4. Variance: the $1/\sqrt{N}$ law

The samples are independent, so variances add and constants pull out squared:

$$\mathrm{Var}\big[\langle I \rangle_N\big]
= \frac{1}{N^2}\sum_{k=1}^{N}\mathrm{Var}\!\left[\frac{f}{p}\right]
= \frac{1}{N}\mathrm{Var}\!\left[\frac{f}{p}\right]$$

The noise you actually *see* is the standard deviation, the square root of that:

$$\sigma\big[\langle I \rangle_N\big] = \frac{\sigma[f/p]}{\sqrt{N}}$$

$$\boxed{\;\text{error} \propto \frac{1}{\sqrt{N}}\;}$$

**This is the single most important practical fact in the whole subject.** To halve the noise you
need **four times** the samples. To get one more decimal digit, one hundred times. Brute force does
not work — 1 sample per pixel is noisy, and 4 spp is only twice as good for four times the cost.

Two consequences that shape every real-time renderer:

1. **You will need a denoiser.** Not as a polish step, as a structural component. The sampling
   budget buys you a noisy-but-unbiased estimate and the filter turns it into an image.
2. **Reducing $\mathrm{Var}[f/p]$ is worth far more than increasing $N$.** Which is the next section.

Also note the rate has no $d$ in it. Monte Carlo converges at $1/\sqrt N$ in 2 dimensions and in 200.
That is why it wins for light transport despite being embarrassingly slow in 1D.

## 5. Importance sampling

Variance depends on $\mathrm{Var}[f/p]$ — the spread of the *ratio*, not of $f$. So choose $p$ to
look like $f$ and the ratio flattens out.

In the limit $p(\boldsymbol{\omega}) = f(\boldsymbol{\omega})/I$, the ratio is the constant $I$
everywhere, variance is **exactly zero**, and one sample gives the exact answer. That is useless as
a method (normalising $p$ requires knowing $I$, which is the thing we wanted) but it is the right
target to aim at: *sample where the integrand is large.*

In practice $f$ is a product of terms and we importance-sample the parts we can integrate
analytically — the cosine, or the BRDF lobe — and let the sampling be blind to the parts we cannot,
which is visibility and incoming light. See the AO example below and the GGX lobe sampling in
`common/ImportanceSampling.glsl`.

## 6. Worked example: the AO estimator

From [[Ambient Occlusion]], the quantity we want is

$$\mathrm{AO}(\mathbf{x}) = \frac{1}{\pi}\int_{\Omega} V(\mathbf{x}, \boldsymbol{\omega})\,\cos\theta\, d\omega$$

so here $f(\boldsymbol{\omega}) = V(\boldsymbol{\omega})\cos\theta$ and there is a $1/\pi$ out front.

**With uniform sampling**, $p = 1/(2\pi)$:

$$\langle \mathrm{AO} \rangle = \frac{1}{\pi}\cdot\frac{1}{N}\sum_k \frac{V(\boldsymbol{\omega}_k)\cos\theta_k}{1/(2\pi)}
= \frac{2}{N}\sum_k V(\boldsymbol{\omega}_k)\cos\theta_k$$

Every ray must be weighted by its own $\cos\theta$. Rays near the horizon are nearly worthless but
cost exactly as much to trace as the valuable ones — wasted work, and extra variance from the wide
spread of weights.

**With cosine-weighted sampling**, $p(\boldsymbol{\omega}) = \cos\theta/\pi$ (this integrates to 1
over the hemisphere, by the $\int\cos\theta\,d\omega = \pi$ result in [[Solid Angle]]):

$$\langle \mathrm{AO} \rangle = \frac{1}{\pi}\cdot\frac{1}{N}\sum_k \frac{V(\boldsymbol{\omega}_k)\cos\theta_k}{\cos\theta_k/\pi}
= \frac{1}{N}\sum_k V(\boldsymbol{\omega}_k)$$

The cosine cancels completely. $V$ is 0 or 1, so:

> **Ambient occlusion is the fraction of cosine-distributed rays that escape.**

Same estimator, same unbiasedness, strictly less variance, and less arithmetic per sample. This is
importance sampling paying for itself, and it is the formal version of the "count how many rays get
out" intuition — the intuition is exactly right *provided the rays are cosine-distributed and confined
to the upper hemisphere*.

### Drawing cosine-weighted directions

**Malley's method**: sample a point uniformly on the unit disk, then project it straight up onto the
hemisphere. The projection warps a uniform disk into exactly the cosine distribution — the Jacobian
of "project up" is the cosine. Two random numbers, no trigonometry beyond a square root:

$$r = \sqrt{u_1}, \quad \varphi = 2\pi u_2, \quad
(x, y, z) = \left(r\cos\varphi,\; r\sin\varphi,\; \sqrt{1 - r^2}\right)$$

in a frame where $z$ is the normal.

## 7. Where the noise actually comes from

Worth internalising, because it predicts what your images will look like:

- $\mathrm{Var}[f/p]$ is large when $f$ is **spiky** relative to $p$ — a small bright light, a narrow
  specular lobe, a thin gap letting light through.
- Visibility is the term nobody can importance-sample, because knowing where the openings are means
  having already solved the problem. It is the irreducible source of noise in AO.
- Low-discrepancy sequences (Hammersley, Sobol, blue noise) do not reduce variance in the above
  sense; they *correlate* the samples so the error is spread more evenly and lands in frequencies a
  filter handles better. They are a different lever from importance sampling and stack with it.

## Sources

- **PBRT 4th edition, Chapter 2 "Monte Carlo Integration"** — <https://pbr-book.org/4ed/Monte_Carlo_Integration>.
  Free, rigorous, and the standard reference. Read 2.1–2.3 for the estimator and variance, then
  Chapter A.5 for sampling techniques.
- **Eric Veach, "Robust Monte Carlo Methods for Light Transport Simulation" (PhD thesis, 1997)** —
  where multiple importance sampling comes from. Heavy, but the source of most modern practice.
- **Scratchapixel, "Monte Carlo Methods in Practice"** — <https://www.scratchapixel.com/> — the
  gentlest correct introduction, with the probability background spelled out.
- **Alan Wolfe's blog** (<https://blog.demofox.org/>) — excellent, very concrete posts on variance,
  blue noise and low-discrepancy sequences.
