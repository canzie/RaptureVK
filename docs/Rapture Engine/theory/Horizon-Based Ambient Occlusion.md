# Horizon-Based Ambient Occlusion

How to turn the [[Ambient Occlusion]] hemisphere integral into a handful of 1D marches across a depth
buffer, and why marching in 2D produces a correct 3D answer.

Prerequisites: [[Ambient Occlusion]], [[Solid Angle]].

---

## 1. A slice is one half of a split integral

Start from the AO integral and write it in spherical coordinates, using
$d\omega = \sin\theta\, d\theta\, d\varphi$ from [[Solid Angle]]:

$$\mathrm{AO} = \frac{1}{\pi}\int_{\Omega} V\,\cos\theta\; d\omega
= \frac{1}{\pi}\int_{0}^{2\pi}\!\!\int_{0}^{\pi/2} V(\theta,\varphi)\;\cos\theta\;\sin\theta\; d\theta\, d\varphi$$

where:

- $\varphi$ — **azimuth**: which way around the normal you are facing. The outer integral.
- $\theta$ — **polar angle** from the normal, as before. The inner integral.
- $V(\theta,\varphi)$ — visibility in that direction.

Nothing has been approximated yet. This is just the same integral written as a nested pair.

> A **slice** is one fixed value of $\varphi$: the half-plane that sticks out of the surface point in
> that compass direction, containing the normal. The inner integral sweeps up that half-plane from
> the surface ($\theta = \pi/2$) to the normal ($\theta = 0$).

So the strategy is a **dimensional decomposition**: rather than scattering sample points over a 2D
hemisphere, pick a few azimuths, solve each 1D inner integral, and average the results. SSAO samples
a volume; HBAO solves slices.

### Picture it

Stand on a flat plain. "Up" is the normal. Pick a compass direction — north. Now imagine a huge sheet
of glass standing vertically, running north from your feet and reaching up to straight overhead. That
half-plane is a slice. The directions you can look *within* the glass form a quarter-circle arc: from
horizontal-north, tilting up ninety degrees to vertical.

There are two natural ways to carve up a hemisphere and they are opposites:

- **Rings** — circles at constant angle from the normal, like lines of *latitude*.
- **Wedges** — half-planes containing the normal, like lines of *longitude*, or segments of an orange.

HBAO uses **wedges**, because a straight line drawn across the screen traces out a plane in the world
(see §5b), and a plane through the shading point is a pair of opposite wedges.

In the integral a slice is a single azimuth — a plane of zero thickness. In an implementation you take
$S$ of them and each sampled plane *stands in for* the wedge of azimuth around it, which is just a
Riemann sum on the outer integral.

## 2. Elevation is the natural variable

Measuring from the normal is awkward for a march, because a march starts at the surface and works
upward. Substitute the **elevation angle** measured from the tangent plane:

$$\alpha = \frac{\pi}{2} - \theta$$

so $\alpha = 0$ lies along the surface and $\alpha = \pi/2$ points straight up the normal. Then
$\cos\theta = \sin\alpha$, $\sin\theta = \cos\alpha$, and the inner integral becomes

$$\int_{0}^{\pi/2} V(\alpha)\,\sin\alpha\,\cos\alpha\; d\alpha$$

## 3. The horizon collapses the inner integral to one number

Here is the assumption that makes the whole method work.

> **Heightfield assumption.** Along a slice, if something blocks the view at elevation $\alpha$, then
> everything below $\alpha$ is blocked too.

That is exactly true for a terrain heightfield and approximately true for most geometry seen from
one viewpoint. Under it, visibility along a slice is a **step function** with a single edge:

$$V(\alpha) = \begin{cases} 0 & \alpha < h \\ 1 & \alpha \ge h \end{cases}$$

where $h$ is the **horizon angle** — the highest elevation at which anything blocks the view. One
scalar now describes the entire inner integral, and the integral has a closed form:

$$\int_{h}^{\pi/2}\sin\alpha\,\cos\alpha\; d\alpha
\;\overset{u=\sin\alpha}{=}\; \int_{\sin h}^{1} u\, du
= \frac{1 - \sin^2 h}{2} = \frac{\cos^2 h}{2}$$

Substituting back:

$$\boxed{\;\mathrm{AO} = \frac{1}{2\pi}\int_{0}^{2\pi} \cos^{2}\!\big(h(\varphi)\big)\; d\varphi\;}$$

**Sanity checks.** Nothing blocking anywhere means $h \equiv 0$, giving
$\frac{1}{2\pi}\cdot 2\pi \cdot 1 = 1$. Fully walled in means $h \equiv \pi/2$, giving $0$. Both
correct.

Discretised over $S$ slices:

$$\mathrm{AO} \approx \frac{1}{S}\sum_{k=1}^{S}\cos^{2} h_k$$

**This is the payoff.** The problem is no longer "integrate visibility over a hemisphere". It is
"find $S$ horizon angles". Everything else is arithmetic.

> **Note on the original paper.** Bavoil et al. write their result as
> $\frac{1}{2\pi}\int (\sin h - \sin t)\, d\varphi$, with $t$ a *tangent angle*. That form measures
> elevation against the tangent plane in a slice built around the **view vector** rather than the
> normal, and carries an approximation the derivation above does not. The mismatch between "slice
> plane contains the view vector" — which is what screen space gives you — and "slice plane contains
> the normal" — which is what the derivation wants — is precisely the gap
> [[Ground Truth Ambient Occlusion]] closes properly.

## 4. Marching a slice in screen space

To find $h$ for one slice:

1. Pick a 2D direction $\mathbf{d}$ on the screen for this slice's azimuth.
2. Step along that line in pixel increments from the current pixel.
3. At each step, read the depth buffer and **reconstruct the sample's view-space position**.
4. Form the 3D vector from the shading point to that sample, and compute its elevation angle.
5. Keep the largest elevation seen. That is $h$.

In pseudocode:

```
P = view-space position of this pixel      // from depth
h = 0                                      // nothing blocking yet

for step in 1..N:
    uv = pixelUV + d * step * stepSize     // walk the screen
    S  = viewPositionFrom(uv, depth(uv))   // back to 3D
    D  = S - P                             // real 3D direction
    elevation = asin( dot(normalize(D), n) )
    h = max(h, elevation)
```

Two details worth noting immediately:

- **One screen line gives two slices.** Marching $+\mathbf{d}$ and $-\mathbf{d}$ covers azimuths
  $\varphi$ and $\varphi + \pi$, which are different slices. $S$ line marches therefore cover $2S$
  azimuths.
- **`max` is a monotone reduction.** Later samples can only refine the answer, never undo it. That
  matters for noise; see §6.

## 5. Why marching in 2D is correct in 3D

This is the part that looks like a cheat and is not. Three separate facts stack up.

### (a) A pixel plus its depth is a 3D point

The depth buffer is not a 2D image of the scene, it is a **3D point cloud in disguise**. Given pixel
coordinates and the depth stored there, you can invert the projection and recover the exact
view-space position of the surface visible at that pixel. `cameraViewPositionFromLinearDepth` in
`common/CameraCommon.glsl` is that inversion.

So walking a line of pixels means visiting a sequence of genuine 3D points that lie on real geometry.
The screen march is a *schedule* for which 3D points to inspect — nothing more.

### (b) A line on the screen is a plane in the world

This is the fact that earns the name "slice".

A straight line on the image plane, together with the camera position, defines a **plane** in 3D: the
plane containing the eye and that line. Every pixel along the screen line has its 3D point somewhere
in that plane, because projection maps points to the ray joining them to the eye, and all those rays
lie in the plane.

So marching a screen-space line samples the scene along a **planar cross-section** — it slices the
world with a plane through the eye. That is a genuine 3D slice, not a 2D approximation of one.

### (c) All the angles are computed in 3D

At no point is an angle measured on the screen. Each sample is un-projected to a real view-space
position, the difference vector $\mathbf{D} = \mathbf{S} - \mathbf{P}$ is a real 3D direction, and
the elevation comes from a real 3D dot product against the real normal. Screen space chooses *where*
to look; view space is where the maths happens.

### The catch, stated honestly

The slice plane from (b) passes through the **eye**. The derivation in §1–3 assumed the slice plane
contains the **normal**. When the surface faces the camera these coincide, and at grazing angles they
diverge — the plane you actually sampled is not the plane whose integral you solved.

HBAO absorbs this with the tangent-angle formulation and a bias term. GTAO handles it exactly, by
projecting the normal onto the slice plane and weighting each slice by the length of that projection.
That projection length is the Jacobian of the mismatch, and it is why
[[Ground Truth Ambient Occlusion]] can claim to converge to a reference where HBAO cannot.

The other limits are the ones every screen-space method inherits, listed in [[Ambient Occlusion]] §6:
nothing off-screen, no thickness, one surface per pixel.

## 6. Choosing the slice and step counts

### How the azimuths are divided

Evenly spaced over a **half** turn:

$$\varphi_k = \frac{k}{S}\,\pi, \qquad k = 0, 1, \dots, S-1$$

$\pi$ rather than $2\pi$ because each slice is a full *line* through the pixel — marching
$+\mathbf{d}$ and $-\mathbf{d}$ already covers $\varphi$ and $\varphi + \pi$. Spacing over a half turn
is what keeps them evenly spread rather than doubled up.

The azimuths are also **rotated per pixel and per frame**. Without that, every pixel shares the same
directional bias and the error is structured — banding, which no filter can remove. With it the error
becomes noise, which filters. Same trade as in [[Monte Carlo Integration]] §2.

Total cost is

$$\text{taps} = S \times \text{steps} \times 2$$

and the pass is texture-fetch bound, so this is the number that matters.

### Slices and steps fix different errors

- **More slices** improve the *outer* integral: azimuthal coverage. They fix "only three compass
  directions were checked".
- **More steps** improve the *inner* integral: locating the horizon. They fix "the march stepped
  straight over the thing that was actually blocking".

Raising one while starving the other just buys more badly-estimated horizons.

### Sampling per pixel is the expensive way to buy slices

Because each pixel rotates its azimuths differently, the spatial filter afterwards is not blurring an
estimate — it is **finishing** one. Neighbouring pixels sampled directions this pixel never did.

$$\text{effective slices} \;\approx\; S \times (\text{filter footprint}) \times (\text{temporal frames})$$

Three slices under a $3\times3$ filter with a few frames of history is already an order of magnitude
more azimuthal coverage than the per-pixel count suggests. This is why GTAO ships with 2–3 slices and
does not look like it: the sampling budget is deliberately tiny because reconstruction does the work.
Widening the filter or deepening the history is usually a better trade than raising $S$.

### What more slices cannot fix

More sampling reduces **variance**. It does nothing for **bias**. These remain wrong at infinite
slices:

- the heightfield assumption — overhangs and floating geometry
- off-screen geometry
- thickness — a surface is still treated as infinitely solid
- one depth layer per pixel
- the slice plane containing the **eye** rather than the **normal** (§5)

At infinite $S$, HBAO converges to a very smooth, very stable, **wrong** answer.

> Sampling buys precision, not correctness. Correctness needs a better method — which is exactly what
> [[Ground Truth Ambient Occlusion]] is, and it targets that last bullet specifically.

Because the azimuths are evenly spaced *and* randomly rotated, this is **stratified** sampling rather
than pure random sampling: for a smooth integrand the error falls roughly as $1/S$ instead of
$1/\sqrt{S}$. The horizon function is discontinuous at silhouettes, which degrades that somewhat, but
it stays better than independent random azimuths.

## 7. Why this beats scattering points

Compare like for like — $N$ texture reads per pixel:

**SSAO** takes $N$ independent point samples, each answering one yes/no question. The estimate is a
binomial proportion, so its variance is $p(1-p)/N$ and the result is visibly noisy at small $N$.
Every sample is thrown away except as one bit.

**HBAO** extracts a *continuous* angle from the same taps, and combines them with `max` rather than by
averaging independent decisions. A single well-placed sample can pin down the entire inner integral.
There is no cancellation between samples and no binomial noise floor.

Two secondary wins:

- **Cache coherence.** March steps read neighbouring texels along a line; scattered samples do not.
- **Structured error.** What error remains is a mis-estimated angle — smooth and low-frequency, and
  it filters well. Binary sampling error is salt-and-pepper.

## 7. Practical details a real implementation needs

- **Screen-space radius.** A world-space radius $r$ covers fewer pixels the further away the surface
  is. The conversion is $r_{\text{px}} = r \cdot P_{00} \cdot \tfrac{1}{2}W / z$, where $P_{00}$ is
  the projection matrix's x-scale, $W$ the render width and $z$ the linear view depth.
- **Jittering.** Starting every pixel's march at the same offset produces banding. Offsetting the
  start per pixel converts that into noise, which a filter can remove. Rotating the slice azimuths
  per pixel does the same for the outer integral.
- **Falloff.** Samples beyond the radius must fade out rather than cut off, or geometry crossing the
  boundary pops. See ambient obscurance in [[Ambient Occlusion]] §4.
- **Bias.** A flat but finely tessellated surface will find its own neighbours slightly above the
  tangent plane, from depth precision and interpolation, and darken itself. A small angular bias on
  $h$ suppresses this at the cost of losing very shallow contact.

## Sources

- **Bavoil, Sainz, Dimitrov, "Image-Space Horizon-Based Ambient Occlusion", SIGGRAPH 2008 talk**
  (NVIDIA) — the original, and short.
- **Bavoil & Sainz, "Multi-Layer Dual-Resolution Screen-Space Ambient Occlusion", SIGGRAPH 2009** —
  follow-up addressing the single-layer limitation.
- **Max, "Horizon Mapping: shadows for bump-mapped surfaces", 1988** — where the horizon-angle idea
  originates, for terrain self-shadowing.
- **Jimenez et al., SIGGRAPH 2016** — GTAO; its background section is a good compact review of what
  HBAO gets wrong and why.
