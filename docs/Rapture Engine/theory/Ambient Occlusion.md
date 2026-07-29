# Ambient Occlusion

What it actually is, where the formula comes from, and the family of techniques that compute it.

Prerequisite: [[Solid Angle]]. Companion: [[Monte Carlo Integration]] — sections 1–5 here need only
solid angle, and §6 onward assumes it.

---

## 1. Where it comes from: the rendering equation

Everything in physically-based rendering descends from Kajiya's rendering equation (1986). For a
point $\mathbf{x}$ with normal $\mathbf{n}$, viewed from direction $\boldsymbol{\omega}_o$:

$$L_o(\mathbf{x}, \boldsymbol{\omega}_o) = L_e(\mathbf{x}, \boldsymbol{\omega}_o) + \int_{\Omega} f_r(\mathbf{x}, \boldsymbol{\omega}_i, \boldsymbol{\omega}_o)\; L_i(\mathbf{x}, \boldsymbol{\omega}_i)\; \cos\theta_i\; d\omega_i$$

where:

- $\mathbf{x}$ — the point being shaded, a position in 3D.
- $\mathbf{n}$ — the surface normal at $\mathbf{x}$, a **unit vector**.
- $\Omega$ — the hemisphere of directions above the surface; the domain the integral runs over.
- $\boldsymbol{\omega}_i, \boldsymbol{\omega}_o$ — incoming and outgoing directions, both **unit
  vectors** rather than angles. Think of each as a point on the unit sphere.
- $\theta_i$ — the angle between $\boldsymbol{\omega}_i$ and the normal $\mathbf{n}$. Zero means
  straight up, $\pi/2$ means grazing along the surface.
- $L_o$ — radiance leaving towards the viewer.
- $L_e$ — radiance the surface emits itself.
- $f_r$ — the **BRDF**: what fraction of light arriving from $\boldsymbol{\omega}_i$ leaves towards
  $\boldsymbol{\omega}_o$.
- $L_i$ — radiance arriving from direction $\boldsymbol{\omega}_i$.
- $\cos\theta_i$ — Lambert's cosine law (see [[Solid Angle]]).
- $d\omega_i$ — a differential patch of solid angle around $\boldsymbol{\omega}_i$, in steradians.
  Note this is *not* "$d$ times $\boldsymbol{\omega}_i$" and not the derivative of anything; it is a
  small patch of directions surrounding $\boldsymbol{\omega}_i$.

It is not solvable in general: $L_i$ at one point is $L_o$ from another, so it is recursive over the
whole scene.

## 2. Three assumptions turn it into AO

**(1) The surface is Lambertian.** $f_r = \rho/\pi$, a constant, so it comes out of the integral.

**(2) Incoming light is uniform and white.** $L_i(\boldsymbol{\omega}) = L$, a constant. This is the
"ambient" assumption — pretend the surface sits inside a uniformly glowing sphere. It is the big lie,
and everything wrong with AO traces back to it.

**(3) The only thing that varies is whether light reaches $\mathbf{x}$ at all.** Introduce the
**visibility function**:

$$V(\mathbf{x}, \boldsymbol{\omega}) = \begin{cases} 0 & \text{if a ray from } \mathbf{x} \text{ along } \boldsymbol{\omega} \text{ hits geometry} \\ 1 & \text{otherwise} \end{cases}$$

Dropping emission and substituting:

$$L_o = \frac{\rho}{\pi} L \int_{\Omega} V(\mathbf{x}, \boldsymbol{\omega})\, \cos\theta\, d\omega$$

where:

- $\rho$ — the surface **albedo**, the fraction of incident light it reflects. A constant in $[0,1]$.
- $L$ — the (assumed uniform) incoming radiance.
- $V$ — the visibility function defined above.

From [[Solid Angle]] we know $\int_{\Omega}\cos\theta\,d\omega = \pi$, so with nothing blocking
($V \equiv 1$) this gives $L_o = \rho L$ — the surface reflects its albedo's worth of the ambient
light, as it should. That tells us the natural normalisation, and gives the definition:

$$\boxed{\;\mathrm{AO}(\mathbf{x}) = \frac{1}{\pi}\int_{\Omega} V(\mathbf{x}, \boldsymbol{\omega})\; \cos\theta\; d\omega\;}$$

so that

$$L_o = \rho \, L \cdot \mathrm{AO}(\mathbf{x})$$

**AO is a scalar in $[0, 1]$ that multiplies ambient light.** 1 = fully open sky, 0 = fully enclosed.
That is literally what the shader does:

```glsl
indirectDiffuse = irradiance * (albedo / PI) * kD_indirect * ao;
```

### Where did the cosine go?

There is no $\cos\theta$ in that line, and there should not be — it is already inside `irradiance`.

**Radiance** $L$ is power per unit area *per steradian*: light along one direction.
**Irradiance** $E$ is power per unit area, total, arriving from every direction:

$$E = \int_{\Omega} L_i(\boldsymbol{\omega})\,\cos\theta\, d\omega$$

where $E$ is the **irradiance** at $\mathbf{x}$, in watts per square metre — as against radiance
$L$, which is watts per square metre *per steradian*.

The cosine is part of the *definition of irradiance*. So the Lambertian form of the rendering
equation,

$$L_o = \frac{\rho}{\pi}\int_{\Omega} L_i \cos\theta\, d\omega$$

collapses to

$$L_o = \frac{\rho}{\pi}\, E$$

DDGI probes store irradiance, so the cosine was applied once when the probe was integrated. By the
time the lighting shader reads the field, that integral is done.

> Whenever a cosine appears to be missing, check whether the quantity in hand is radiance or
> irradiance. Most "my lighting is $\pi$ times off" bugs are this.

### Why $\rho/\pi$ and not $\rho$

Energy conservation. A Lambertian surface has a constant BRDF $f_r = c$, and the fraction of incident
power it re-emits must be its albedo $\rho$:

$$\int_{\Omega} c\,\cos\theta\, d\omega = c\,\pi \;\overset{!}{=}\; \rho
\quad\Longrightarrow\quad c = \frac{\rho}{\pi}$$

Check it end to end with uniform light and nothing blocking: $E = L\pi$, so
$L_o = \frac{\rho}{\pi}\cdot L\pi = \rho L$. Omit the $1/\pi$ and the surface is $\pi$ times too
bright.

### The two $\pi$'s are not the same $\pi$

Both descend from $\int_{\Omega}\cos\theta\,d\omega = \pi$, but they do different jobs and are not
cancelling one another:

| Where | Job |
|---|---|
| $\rho/\pi$ in the BRDF | energy conservation; converts irradiance into outgoing radiance |
| $\frac{1}{\pi}$ in the AO definition | normalisation, so unoccluded $= 1$ and AO is dimensionless |

AO's $\pi$ is what lets `irradiance * (albedo/PI) * ao` have consistent units at all.

### Why the factorisation is allowed

Strictly the quantity wanted is $\int_{\Omega} V\,L_i \cos\theta\, d\omega$, one integral. Splitting
it into (irradiance) $\times$ (AO), computed separately and multiplied, is **exact only when $L_i$ is
uniform** — a product of averages equals the average of a product only for uncorrelated terms.

That is assumption (2) reappearing in a different costume, and it is the precise statement of why AO
is an approximation rather than a term in the equation.

## 3. Reading the formula

### $\cos\theta$ in the maths, `dot(N, L)` in the code

These docs write the cosine factor as $\cos\theta$ rather than as the dot product
$(\boldsymbol{\omega}\cdot\mathbf{n})$ you will see in most papers. They are the same number: for any
two vectors $\mathbf{a}\cdot\mathbf{b} = |\mathbf{a}||\mathbf{b}|\cos\theta$, and both of ours are
unit length, so $\boldsymbol{\omega}\cdot\mathbf{n} = \cos\theta$ exactly.

$\cos\theta$ says what the term *means*; the dot product says how it is *computed*. Note that the
dot product yields $\cos\theta$, **not** $\theta$ — recovering the angle needs $\arccos$, which is
rarely wanted.

| $\theta$ | $\cos\theta$ | |
|---|---|---|
| $0$, along $\mathbf{n}$ | $1.0$ | full weight |
| $60^\circ$ | $0.5$ | half weight |
| $90^\circ$, grazing | $0.0$ | no weight |
| $> 90^\circ$, below the surface | $< 0$ | outside $\Omega$ |

In shader code this term is `max(dot(N, L), 0.0)`. The `max` is there because a light direction can
point below the surface; inside $\Omega$ the cosine is non-negative by construction.

### What each part is doing physically

Three things are doing work, and each has a physical reason:

- **The hemisphere $\Omega$, not a sphere.** Directions below the surface are inside the object.
  They are not "blocked", they are not directions light can arrive from at all.
- **The cosine weight.** Directions along the normal matter most; directions at the horizon
  contribute nothing. An occluder standing right at the horizon blocks almost no light.
- **The $1/\pi$.** Normalisation so the unoccluded answer is exactly 1.

> ### On your voxel intuition
> Counting air voxels in a **sphere** around the point gets the shape of the answer but is wrong in
> two specific ways: it includes the lower hemisphere (which should never count), and it weights all
> directions equally (missing the cosine). Uncorrected, a flat unoccluded floor scores $0.5$ instead
> of $1.0$ — the ground below it counts as "occlusion".
>
> Both are fixable: use only the upper hemisphere, and weight each direction by $\cos\theta$. Do that
> and the count *is* the integral. Section 6 of [[Monte Carlo Integration]] shows that if you draw the
> directions cosine-distributed, the weighting cancels out and AO is exactly the fraction of rays that
> escape — so the counting intuition is right, once the counting is done in the right distribution.

## 4. Ambient obscurance: the distance limit

The definition above has a fatal problem indoors. In a closed room every ray hits a wall eventually,
so $V \equiv 0$ and $\mathrm{AO} = 0$ everywhere. The room is uniformly black.

The fix, from Zhukov et al. (1998), is to make visibility fall off with distance:

$$\mathrm{AO}(\mathbf{x}) = \frac{1}{\pi}\int_{\Omega} \kappa\big(d(\mathbf{x},\boldsymbol{\omega})\big)\, \cos\theta\, d\omega$$

where:

- $d(\mathbf{x},\boldsymbol{\omega})$ — distance from $\mathbf{x}$ to the first thing hit along
  $\boldsymbol{\omega}$, or infinity if the ray escapes.
- $\kappa$ — the falloff, rising from 0 at contact to 1 at some radius $r$. It replaces the binary
  $V$: a distant occluder now counts for nothing rather than for everything. (Zhukov writes this
  $\rho$; here that letter is already the albedo, so $\kappa$ avoids the collision.)

Strictly this is **ambient obscurance**, not ambient occlusion; everyone calls it AO anyway.

This is what the `radius` parameter on every AO implementation is. It is not a quality setting — it
is the scale at which you have decided occlusion stops counting as occlusion and starts counting as
"the room". It is also why a 1-metre radius did nothing visible on kilometre-scale terrain.

## 5. AO is a hack, and it is worth knowing why

Assumption (2) — uniform incoming light — is false in every real scene. Light comes from windows,
lamps, the sky in one direction. AO darkens a crevice by the same amount whether it faces the sun or
faces away, because it never knew where the light was.

With true global illumination you do not need AO: the light transport already accounts for occlusion,
correctly and directionally. So why does this engine have both?

Because **DDGI is spatially coarse**. It is a grid of probes with a handful of octahedral texels each,
so it resolves occlusion at the scale of a probe cell — metres. The darkening where two walls meet, or
where an object touches the floor, is far below that resolution. AO is a cheap high-frequency
correction on top of a correct low-frequency solution. That division of labour is the justification,
and it is why AO multiplies the *indirect* diffuse term and never the direct light (shadow maps
already do that job, properly and with a real light direction).

A **bent normal** — the average unoccluded direction, computed alongside the scalar — partially
recovers the directionality that assumption (2) throws away.

Assumption (1) costs something too. Setting $f_r = \rho/\pi$ discards the BRDF, which is why one AO
value has to serve every material and why occluding a narrow specular lobe with it is wrong (see
`specularOcclusion`). It also quietly assumes the surface is **opaque**: the whole argument for
integrating over a hemisphere rather than a sphere was "directions below the surface are inside the
object". For a transmissive surface that is false — light does arrive through it, which is what a
BTDF describes, and the correct integral runs over the full sphere against a BSDF. So AO is
structurally wrong on glass, foliage, and anything with meaningful subsurface transport, not merely
inaccurate.

## 6. The family of techniques

All of them compute the same integral. They differ in how they answer "does a ray in this direction
hit anything?", and that choice determines both the cost and the failure modes.

### Ray-traced (RTAO) — the reference

Trace $N$ actual rays against the scene's acceleration structure, cosine-distributed, count the
escapes. This *is* the definition, so it is correct by construction; its only error is the variance
of [[Monte Carlo Integration]], which shrinks as $1/\sqrt{N}$.

Cost is $N$ rays per pixel. With a BVH already built (this engine has one for DDGI), it is the
simplest AO to implement correctly — no heuristics, no thickness guesses, no screen-space caveats.

### Object-space / baked

Compute AO offline per vertex or into a texture. Free at runtime, exact, and completely static — it
cannot respond to anything moving. This is what the `ao` channel in a glTF material is.

### Voxel / volumetric

Rasterise the scene into a voxel grid, then cone-trace or count occupancy against it. The version you
saw. Handles dynamic scenes and off-screen geometry, at the cost of a voxelisation pass, memory, and
resolution limited by voxel size.

### Screen-space (SSAO) — Crytek, 2007

The key move: **use the depth buffer as a cheap stand-in for the scene**. Sample points in a sphere
around the pixel, project each into screen space, and compare its depth against the depth buffer. If
the sample is behind what the buffer records, call it occluded.

Fast — no scene structure, no rays, just texture reads. And wrong in four ways that every screen-space
technique inherits:

1. **No off-screen geometry.** A wall just outside the frame casts no occlusion, and the AO changes
   as you turn the camera.
2. **No thickness.** The depth buffer stores a surface, not a solid. Everything behind that surface
   is assumed to be filled in forever, so thin objects over-occlude.
3. **Nothing behind the first surface.** One depth value per pixel; whatever it hides does not exist.
4. **View dependence.** The answer is a property of the camera as well as the scene.

### Horizon-based (HBAO) — Bavoil et al., 2008

The refinement that everything modern is built on. Instead of scattering sample points in a volume,
pick a few **slices** — lines through the pixel in screen space — and march along each one looking for
the **horizon**: the largest elevation angle anything reaches. Everything below that angle is blocked,
everything above is open.

Two wins. It turns a 3D sampling problem into a handful of 2D line marches, and it extracts a
*continuous* angle from each march rather than a binary hit, so the same number of texture reads
carries far more information and the result is much less noisy.

(The idea traces back to horizon mapping for terrain self-shadowing, Max 1988.)

### Ground-truth (GTAO) — Jimenez et al., 2016

Keeps HBAO's horizon search exactly, and changes what happens next. HBAO *estimates* the visibility
from the horizon angles with a heuristic. GTAO plugs the horizon angles into the **closed-form
solution** of the cosine-weighted integral over the arc they leave open.

The sampling then only has to *find the horizon*; the integration is exact. That is the whole
difference, and it is why the name is earned: it converges to the ray-traced reference, where HBAO
converges to a tuned approximation.

---

## Where we go next

The plan, in order:

1. **[[Horizon-Based Ambient Occlusion]]** — the slice geometry, what a horizon angle is, how you
   find one by marching a depth buffer. Everything except the final integral.
2. **[[Ground Truth Ambient Occlusion]]** — the closed-form arc integral, its derivation, and the
   bent normal that comes with it.
3. Implementation: I write the pass class and the Vulkan plumbing, you write the shader.

## Sources

- **Kajiya, "The Rendering Equation", SIGGRAPH 1986** — the origin. Short and readable.
- **Zhukov, Iones, Kronin, "An Ambient Light Illumination Model", EGWR 1998** — introduces obscurance
  and the distance falloff.
- **Landis, "Production-Ready Global Illumination", SIGGRAPH 2002 course (ILM)** — what popularised AO
  in film, and still one of the clearest explanations of why it looks right.
- **Mittring, "Finding Next Gen — CryEngine 2", SIGGRAPH 2007 course** — the original SSAO.
- **Bavoil, Sainz, Dimitrov, "Image-Space Horizon-Based Ambient Occlusion", SIGGRAPH 2008 talk** (NVIDIA)
  — HBAO.
- **Jimenez, Wu, Pesce, Jarabo, "Practical Realtime Strategies for Accurate Indirect Occlusion",
  SIGGRAPH 2016** — GTAO. Paper and slides are on the Activision Research publications page. This is
  the one we will be implementing; the slides are more useful than the paper.
- **Intel XeGTAO** — <https://github.com/GameTechDev/XeGTAO> — a well-commented open implementation,
  and the `XeGTAO.h` header is worth reading for the practical details the paper omits.
