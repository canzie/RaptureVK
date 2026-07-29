# Ground Truth Ambient Occlusion

GTAO keeps [[Horizon-Based Ambient Occlusion]]'s horizon search unchanged and replaces what happens
afterwards: the horizon angles feed a **closed-form solution** of the cosine-weighted visibility
integral instead of a heuristic. The sampling only has to locate the horizon; the integration is
exact. That is the whole of the difference, and it is why the name is earned — it converges to a
ray-traced reference where HBAO converges to a tuned approximation.

Prerequisites: [[Horizon-Based Ambient Occlusion]], [[Ambient Occlusion]], [[Solid Angle]].

---

## 1. Reparametrising around the view vector

HBAO measures elevation from the **tangent plane** and assumes the slice plane contains the normal.
It does not — a screen-space line gives a plane through the **eye** (see [[Horizon-Based Ambient
Occlusion]] §5). GTAO fixes this by measuring everything from the view vector instead, which *is* in
every slice plane by construction.

Set up spherical coordinates with the **view vector $\mathbf{V}$ as the pole**:

- $\varphi \in [0, \pi]$ — which slice *plane*. Half a turn, because one plane holds two slices.
- $\theta \in [-\pi/2, \pi/2]$ — signed angle from $\mathbf{V}$ within that plane. The sign picks
  which of the two half-planes.

In these coordinates the measure is

$$d\omega = |\sin\theta|\; d\theta\; d\varphi$$

which is the usual $\sin\Theta\, d\Theta\, d\Phi$ with $\Theta = |\theta|$, the absolute value
folding the two half-planes into one signed range.

## 2. Where the projected normal comes from

The AO integrand weights each direction by $\cos$ of its angle to the **normal**, which is
$\boldsymbol{\omega}\cdot\mathbf{n}$. Split the normal relative to the slice plane:

$$\mathbf{n} = \mathbf{n}_\parallel + \mathbf{n}_\perp$$

where:

- $\mathbf{n}_\parallel$ — the part lying **in** the slice plane (`projN` in the shader).
- $\mathbf{n}_\perp$ — the part along the slice plane's own normal.

Every direction $\boldsymbol{\omega}$ we integrate lies in the slice plane, so
$\boldsymbol{\omega}\cdot\mathbf{n}_\perp = 0$ exactly, and

$$\boldsymbol{\omega}\cdot\mathbf{n} = \boldsymbol{\omega}\cdot\mathbf{n}_\parallel
= \|\mathbf{n}_\parallel\| \cos(\theta - n)$$

where $n$ is the angle of $\mathbf{n}_\parallel$ from $\mathbf{V}$, measured in the slice plane.

> **This is the key point.** $\|\mathbf{n}_\parallel\|$ is not a fudge factor or an empirical weight.
> It is what the dot product *becomes* when you decompose the normal — the exact Jacobian of
> projecting the hemisphere onto the slice plane. HBAO's error is silently setting it to 1.

Putting it together:

$$\mathrm{AO} = \frac{1}{\pi}\int_{0}^{\pi} \|\mathbf{n}_\parallel\|
\int_{h_1}^{h_2} \cos(\theta - n)\,|\sin\theta|\; d\theta\; d\varphi$$

Discretised over $S$ slices, the $\tfrac{1}{\pi}\cdot\tfrac{\pi}{S}$ collapses and you get

$$\mathrm{AO} \approx \frac{1}{S}\sum_{k=1}^{S} \|\mathbf{n}_\parallel\|_k \; a(h_{1,k}, h_{2,k}, n_k)$$

## 3. Building the slice frame

```glsl
vec3 viewDir = normalize(-P);
vec3 marchDir = vec3(dir.x / cam.proj[0][0], dir.y / cam.proj[1][1], 0.0);
vec3 slicePlaneNormal = normalize(cross(marchDir, viewDir));
vec3 tangent = normalize(cross(viewDir, slicePlaneNormal));

vec3 projN = viewNormal - slicePlaneNormal * dot(viewNormal, slicePlaneNormal);
float projNLength = length(projN);
float n = atan(dot(projN, tangent), dot(projN, viewDir));
```

Two orderings matter and both fail silently:

- **`marchDir` must undo the projection.** Writing `vec3(dir, 0.0)` treats a screen direction as a
  view direction; under Vulkan's downward $y$ and a non-square aspect they disagree, and the frame
  comes out **mirrored**. A mirrored frame swaps $h_1$ and $h_2$. Visibility survives it (the
  integral is invariant under $(h_1, h_2, n) \to (-h_2, -h_1, -n)$), a bent normal does not.
- **`cross(viewDir, slicePlaneNormal)`, not the reverse.** The other order puts `tangent` on the
  $-\mathbf{d}$ side, which also swaps the two horizons.

## 4. The arc: where the two hemispheres overlap

The integral can only run over directions that are *both* above the surface and in front of the
camera. Two limits:

- the normal's hemisphere: $[\,n - \tfrac{\pi}{2},\; n + \tfrac{\pi}{2}\,]$
- what screen space can represent: $[-\tfrac{\pi}{2},\; \tfrac{\pi}{2}\,]$

Take the intersection:

$$h_1^{\text{open}} = \max\!\left(-\tfrac{\pi}{2},\; n - \tfrac{\pi}{2}\right), \qquad
h_2^{\text{open}} = \min\!\left(\tfrac{\pi}{2},\; n + \tfrac{\pi}{2}\right)$$

Seeding the march with the **cosines** of these limits means samples can only ever close the horizon
further, and the usual post-march clamp disappears:

```glsl
float lowHorizonCos1 = cos(max(-HALF_PI, n - HALF_PI));
float lowHorizonCos2 = cos(min(HALF_PI, n + HALF_PI));
```

## 5. The closed form, derived

We need

$$a(h_1, h_2, n) = \int_{h_1}^{h_2}\cos(\theta - n)\,|\sin\theta|\; d\theta$$

Split at $\theta = 0$ to remove the absolute value (with $h_1 \le 0 \le h_2$):

$$a = \int_{h_1}^{0}\cos(\theta-n)\,(-\sin\theta)\, d\theta + \int_{0}^{h_2}\cos(\theta-n)\,\sin\theta\, d\theta$$

Use the product-to-sum identity:

$$\cos(\theta - n)\sin\theta = \tfrac{1}{2}\big[\sin(2\theta - n) + \sin n\big]$$

which integrates elementarily:

$$\int \cos(\theta-n)\sin\theta \; d\theta = -\frac{\cos(2\theta - n)}{4} + \frac{\theta \sin n}{2}$$

**Second piece**, evaluated from $0$ to $h_2$:

$$\left[-\frac{\cos(2\theta-n)}{4} + \frac{\theta\sin n}{2}\right]_0^{h_2}
= \frac{1}{4}\Big(-\cos(2h_2 - n) + \cos n + 2h_2\sin n\Big)$$

**First piece**, from $h_1$ to $0$ with the sign flipped:

$$-\left[-\frac{\cos(2\theta-n)}{4} + \frac{\theta\sin n}{2}\right]_{h_1}^{0}
= \frac{1}{4}\Big(-\cos(2h_1 - n) + \cos n + 2h_1\sin n\Big)$$

Adding them:

$$\boxed{\;a(h_1,h_2,n) = \tfrac{1}{4}\Big(-\cos(2h_1 - n) + \cos n + 2h_1\sin n\Big)
+ \tfrac{1}{4}\Big(-\cos(2h_2 - n) + \cos n + 2h_2\sin n\Big)\;}$$

**Check.** Flat surface facing the camera: $n = 0$, arc $[-\pi/2, \pi/2]$.

$$\tfrac{1}{4}\big(-\cos(-\pi) + 1 + 0\big) + \tfrac{1}{4}\big(-\cos(\pi) + 1 + 0\big)
= \tfrac{1}{2} + \tfrac{1}{2} = 1 \quad\checkmark$$

Note this needs actual angles, so `acos` returns to the inner path. The trig-free
$\tfrac{1}{2}(1 - \sin^2 h)$ of HBAO only worked because that formulation never needed anything but
the sine.

## 6. Normalising by the open arc

This part is **not in the paper** and it matters.

### The problem, with numbers

Dividing by $S$ assumes every slice's maximum possible contribution is the same. It is not — the arc
from §4 shrinks as $|n|$ grows. Take a surface seen exactly **edge-on** ($\mathbf{n} \perp
\mathbf{V}$, i.e. a silhouette) with *nothing whatsoever in front of it*:

**Every slice has $n = \pm\pi/2$.** Since $\mathbf{n} \perp \mathbf{V}$ and the slice plane normal is
also $\perp \mathbf{V}$, the projection $\mathbf{n}_\parallel$ stays perpendicular to $\mathbf{V}$,
so $n$ is a right angle in every slice.

**The arc is half width.** For $n = \pi/2$ it clamps to $[0, \pi/2]$, and

$$a(0, \tfrac{\pi}{2}, \tfrac{\pi}{2}) = \underbrace{\tfrac{1}{4}\big(-\cos(-\tfrac{\pi}{2}) + 0 + 0\big)}_{0}
+ \underbrace{\tfrac{1}{4}\big(-\cos(\tfrac{\pi}{2}) + 0 + \pi\big)}_{\pi/4} = \frac{\pi}{4}$$

**The weight averages to $2/\pi$.** As $\varphi$ sweeps, the slice normal rotates in the plane
perpendicular to $\mathbf{V}$, so $\|\mathbf{n}_\parallel\| = |\sin\psi|$, whose mean over half a
turn is $\tfrac{1}{\pi}\int_0^\pi |\sin\psi|\,d\psi = \tfrac{2}{\pi}$.

Multiply:

$$\mathrm{AO} = \frac{2}{\pi}\cdot\frac{\pi}{4} = \boxed{\tfrac{1}{2}}$$

**Every silhouette in the scene reads exactly 0.5 with nothing occluding it.** Not approximately —
exactly half.

### The fix

Evaluate the same integral over the arc *before any sample closed it*, and divide by that instead:

```glsl
sum     += projNLength * sliceVisibility(h1, h2, n);
openSum += projNLength * sliceVisibility(openH1, openH2, n);
...
float ao = openSum > 1e-5 ? sum / openSum : 1.0;
```

An unoccluded surface now has $\text{sum} = \text{openSum}$ and reports exactly 1 at **any** view
angle. Cost is one extra `sliceVisibility` call per slice; no new constants.

### It is a modelling choice, not a bug fix

Be honest about what changed. The half of the hemisphere behind the view plane is *unknown* —
screen space has no record of it. The two options are:

| Divisor | Reads the unknown half as | Silhouette of an open surface |
|---|---|---|
| $S$ (the paper) | occluded | $0.5$ |
| $\text{openSum}$ | not occluded | $1.0$ |

Neither is derivable from the maths; both are assumptions about missing data. This engine takes the
second because:

- **It is view-independent.** With the first, rotating the camera around a static unoccluded object
  changes its AO, which reads as flicker rather than as shading.
- **AO should measure occluders.** A dark rim on every silhouette is measuring camera geometry.
- **Absence of evidence is not evidence of occlusion** — the same reasoning that makes an off-screen
  march step `break` rather than count as a blocker.

## 7. What GTAO still does not fix

All of these are **bias**, and no amount of extra sampling touches them:

- **The heightfield assumption.** Overhangs and floating geometry still produce a single horizon
  where the truth has two edges.
- **Thickness.** The depth buffer records a surface; everything behind it is assumed solid forever,
  so thin objects over-occlude.
- **One layer per pixel.** Whatever is hidden stays hidden.
- **Off-screen geometry.** Still absent.

See [[Horizon-Based Ambient Occlusion]] §6 on why more slices cannot help with any of them.

## Sources

- **Jimenez, Wu, Pesce, Jarabo, "Practical Realtime Strategies for Accurate Indirect Occlusion",
  SIGGRAPH 2016** — the paper. Slides are more useful than the text.
- **Intel XeGTAO** — <https://github.com/GameTechDev/XeGTAO> — well-commented open implementation;
  `XeGTAO.h` carries the practical details the paper omits.
- **PBRT chapter 2** — <https://pbr-book.org/4ed/Monte_Carlo_Integration> — for the estimator theory
  underneath the sampling.
