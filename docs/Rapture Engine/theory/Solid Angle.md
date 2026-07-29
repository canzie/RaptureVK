# Solid Angle

The 3D generalisation of an angle. You need it to read any lighting integral, because every one of
them integrates "over all directions", and $d\omega$ is what "a little bit of direction" means.

Used by [[Ambient Occlusion]], [[Monte Carlo Integration]], the rendering equation.

---

## From angle to solid angle

In 2D, an angle is arc length divided by radius:

$$\theta = \frac{s}{r} \quad \text{[radians]}$$

Dividing by $r$ makes it scale-free — a circle subtends $2\pi$ radians whatever its size.

In 3D the same construction with area instead of arc length:

$$\Omega = \frac{A}{r^2} \quad \text{[steradians, sr]}$$

where $A$ is the area a shape projects onto a sphere of radius $r$ centred on the viewer.

- Whole sphere: $A = 4\pi r^2$, so $\Omega = 4\pi$ sr.
- Hemisphere: $\Omega = 2\pi$ sr.

A solid angle is "how much of your field of view something takes up", independent of distance. The
Sun and the Moon have wildly different sizes and distances but almost the same solid angle from
Earth (~$6\times10^{-5}$ sr), which is why eclipses line up so neatly.

## The differential solid angle

Nearly every derivation needs $d\omega$ in spherical coordinates. Put the pole along the surface
normal $\mathbf{n}$:

- $\theta$ — **polar / zenith** angle, measured from $\mathbf{n}$. $\theta = 0$ is straight up,
  $\theta = \pi/2$ is along the surface.
- $\varphi$ — **azimuth**, the rotation around $\mathbf{n}$.

On the unit sphere, take a small patch spanning $d\theta$ and $d\varphi$:

- Moving by $d\theta$ walks along a great circle of radius 1, so that side has length $d\theta$.
- Moving by $d\varphi$ walks around a circle of latitude. At polar angle $\theta$ that circle has
  radius $\sin\theta$, so that side has length $\sin\theta \, d\varphi$.

The patch is approximately a rectangle, so

$$d\omega = \sin\theta \, d\theta \, d\varphi$$

That $\sin\theta$ is not decoration — it is why directions near the pole are "rare" (the circles of
latitude shrink to nothing) and directions near the equator are "common". Forgetting it is the most
common way to get a lighting integral wrong.

**Check it gives the whole sphere:**

$$\int_0^{2\pi}\!\!\int_0^{\pi} \sin\theta \, d\theta \, d\varphi
= 2\pi \Big[-\cos\theta\Big]_0^{\pi} = 2\pi \cdot 2 = 4\pi \quad\checkmark$$

## The cosine-weighted hemisphere integral

This one constant appears everywhere, so it is worth doing once by hand:

$$\int_{\Omega} \cos\theta \, d\omega
= \int_0^{2\pi}\!\!\int_0^{\pi/2} \cos\theta \, \sin\theta \, d\theta \, d\varphi$$

where $\Omega$ is the hemisphere above the surface and $\theta$ is measured from the normal, so the
upper limit is $\pi/2$ rather than $\pi$. The $\sin\theta$ is the $d\omega$ expansion from above, and
the $\cos\theta$ is the weight being applied — two different factors that happen to look alike.

Substitute $u = \sin\theta$, $du = \cos\theta\, d\theta$:

$$= 2\pi \int_0^{1} u \, du = 2\pi \cdot \tfrac{1}{2} = \boxed{\pi}$$

**This is where the $\pi$ in Lambert's BRDF comes from.** A diffuse surface with albedo $\rho$ has
$f_r = \rho/\pi$ precisely so that a fully lit surface reflects exactly $\rho$ of the light hitting
it rather than $\pi\rho$. Every stray $\pi$ in a shader traces back to this integral.

## Why the cosine is there at all

The $\cos\theta$ factor is **Lambert's cosine law**, and it is
geometry rather than a material property. A beam of light with cross-section $A$ striking a surface
at angle $\theta$ from the normal spreads over surface area $A/\cos\theta$. The same energy over
more area means less energy per unit area, by a factor of $\cos\theta$.

So light arriving along the normal counts fully, light arriving at a grazing angle counts for almost
nothing. Any integral that weights all directions equally is describing something other than
irradiance.

## Sources

- **PBRT, "Geometry and Transformations" and "Color and Radiometry"** — <https://pbr-book.org/>.
  Free online, and the definitive treatment. Section 4.1 of the 4th edition covers solid angle
  and the radiometric quantities built on it.
- **Scratchapixel, "Mathematics of Shading"** — <https://www.scratchapixel.com/> — gentler, with
  more pictures.
