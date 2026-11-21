# Sky Atmosphere

How the engine renders a planet's sky and the haze between the camera and the scene. It follows Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020 (Computer Graphics Forum 39(4)). Paper: https://sebh.github.io/publications/egsr2020.pdf. Reference code (MIT): https://github.com/sebh/UnrealEngineSkyAtmosphere, the interesting files are `Resources/RenderSkyRayMarching.hlsl` and `Resources/RenderSkyCommon.hlsl`.

Prerequisite: [[Solid Angle]].

## The medium

The atmosphere is a shell between the ground radius $R_g = 6360$ km and the top $R_t = R_g + 100$ km. Three things live in it, each with a density that only depends on altitude $h$:

- **Rayleigh** (air molecules): scatters, never absorbs. Density $d_r(h) = e^{-h / 8\text{km}}$. Scattering is much stronger for blue, $\sigma_s^r = (5.802, 13.558, 33.1) \times 10^{-6}\,\text{m}^{-1}$, which is why the sky is blue and sunsets are red: blue is scattered out of a long sunset path before it reaches you. The coefficient goes as $\lambda^{-4}$, which is what the editor's wavelength field converts through.
- **Mie** (aerosols: dust, smoke, sea salt, pollution, water droplets): scatters and absorbs, the same for every colour. $\sigma_s^m = 3.996$, $\sigma_a^m = 4.40$ ($\times 10^{-6}\,\text{m}^{-1}$), density $e^{-h / 1.2\text{km}}$. Responsible for the white haze and the bright halo around the sun. More Mie = hazier, whiter sky, browner sunsets.
- **Ozone**: absorbs only, mostly orange. A tent 30 km wide centred at 25 km. It is what keeps the sky blue right after sunset, without it twilight goes a muddy olive.

Extinction is everything that removes light from a beam: $\sigma_t = \sigma_s + \sigma_a$.

Phase functions say where scattered light goes, $\theta$ being the angle between the direction to the sun and the view direction:

$$p_r(\theta) = \frac{3(1 + \cos^2\theta)}{16\pi} \qquad p_m(\theta, g) = \frac{3}{8\pi}\frac{(1-g^2)(1+\cos^2\theta)}{(2+g^2)(1+g^2-2g\cos\theta)^{3/2}}$$

$p_m$ is Cornette-Shanks, $g = 0.8$ makes it strongly forward peaked (the sun halo).

## Single scattering

Light reaching the camera at $c$ along view direction $v$, the ray ending at $p$ (ground, scene or top of the atmosphere):

$$L(c, v) = T(c, p)\, L_o(p, v) + \int_0^{\|p-c\|} L_{scat}(c, c - tv, v)\, dt$$

$$T(x_a, x_b) = e^{-\int_{x_a}^{x_b} \sigma_t(x)\, dx}$$

$$L_{scat}(c, x, v) = \sigma_s(x)\, T(c, x)\, S(x, l)\, p(v, l)\, E$$

$T$ is transmittance, $S$ is the sun's visibility from $x$ (zero in the planet's shadow, otherwise the transmittance from $x$ to the top of the atmosphere towards the sun), $E$ the sun's illuminance at the top of the atmosphere. $L_o$ is the ground: a Lambertian surface with albedo $\rho = 0.3$, lit by the sun. Without it everything below the horizon is black, which is what the old O'Neil shader did.

## The lookup tables

| Table | Size | Built | What it stores |
|---|---|---|---|
| Transmittance | 256×64 | when the atmosphere changes | $T$ from a point to the top of the atmosphere, by altitude and zenith angle |
| Multiple scattering | 32×32 | when the atmosphere changes | $\Psi_{ms}$, by altitude and sun zenith angle |
| Sky view | 192×108 | when the sun, atmosphere or camera altitude changes | the distant sky around the camera, lat/long |
| Aerial perspective | 32×32×32 | every frame | in-scattering and transmittance over the view frustum |

Per the paper's Table 2, on a GTX 1080 these cost 0.01, 0.07, 0.05 and 0.04 ms.

**Transmittance** uses the Bruneton and Neyret 2008 mapping: the altitude goes through $\rho = \sqrt{r^2 - R_g^2}$ (distance to the horizon), and the view angle through the distance $d$ to the top of the atmosphere, remapped between its minimum and maximum for that altitude. It spends texels where transmittance changes fastest, near the horizon.

**Sky view** is a latitude/longitude map around the camera's up vector. Latitude is squeezed quadratically towards the horizon, $v = 0.5 + 0.5\,\text{sign}(l)\sqrt{|l| / (\pi/2)}$, because that is where the sky changes fastest. Longitude is the azimuth *relative to the sun*: the sky is mirror-symmetric about the sun's vertical plane, so camera rotation never invalidates the table.

**Aerial perspective** is a froxel volume: 32×32 over the screen, 32 depth slices spaced quadratically over 32 km. RGB is in-scattered light, A is $1 - \bar T$. It is applied after lighting as $\text{color} = \text{inscatter} + \text{color} \cdot \bar T$.

## Multiple scattering

Real light bounces many times. The usual approach iterates one LUT per scattering order, which is slow. Hillaire assumes that from the second bounce on:

1. scattering is isotropic, $p_u = 1/4\pi$,
2. every point near $x_s$ receives the same light as $x_s$ itself.

Then gather, over the sphere of directions, the second order light arriving at $x_s$ (with unit sun, ground included):

$$L_{2^{nd}order} = \int_{4\pi} L'(x_s, -\omega)\, p_u\, d\omega$$

and the fraction of light the surroundings send back to $x_s$:

$$f_{ms} = \int_{4\pi} \int_0^{\|p - x_s\|} \sigma_s(x)\, T(x_s, x)\, dt\; p_u\, d\omega$$

Each further order is one more $f_{ms}$, so all orders together are a geometric series:

$$F_{ms} = 1 + f_{ms} + f_{ms}^2 + \dots = \frac{1}{1 - f_{ms}} \qquad \Psi_{ms} = L_{2^{nd}order}\, F_{ms}$$

$\Psi_{ms}$ is stored per altitude and sun angle and simply added inside the single scattering integral:

$$L_{scat}(c, x, v) = \sigma_s(x)\,\big(T(c, x)\, S(x, l)\, p(v, l) + \Psi_{ms}\big)\, E$$

64 directions per texel, one thread each, summed in shared memory.

## Segment integration

Within a march step of length $\Delta t$ the scattering is treated as constant but the transmittance is not, so each step integrates it exactly (Hillaire 2015, "Physically Based and Unified Volumetric Rendering in Frostbite", slide 28):

$$\int_0^{\Delta t} S\, e^{-\sigma_t x}\, dx = \frac{S - S\, e^{-\sigma_t \Delta t}}{\sigma_t}$$

## Engine specifics

- Positions in the shaders are kilometres relative to the ground point below the world origin. $|P|^2 - R_g^2$ is computed as $x^2 + z^2 + y(2R_g + y)$, never by subtracting squares: at a 2 m camera height on a 6360 km sphere the subtraction loses all precision and the horizon shimmers.
- The light marked as the atmosphere sun is the $E$ above, top of the atmosphere. The light that reaches the scene is $E \cdot T$, computed on the CPU and folded into the light colour, which is what turns sunset lighting orange.
- The sun disk is drawn only in the sky pass, never into the environment cubemap, since the directional light already lights the scene and a disk in the cubemap would double it and spike the prefiltered specular.
- Sky intensity 1 is physical. The sky is lit by the same sun light as the scene, so brightness is an exposure question.

## Known limits

- Isotropic multiple scattering drifts for very dense atmospheres and strongly forward Mie ($g$ near 1), see Figure 12 of the paper.
- No volumetric shadows (mountains, clouds into the atmosphere). The paper does those with a full screen ray march and shadow maps.
- No moon. It would be a second light: the same tables, with the moon's direction and roughly $1/400\,000$ of the sun's illuminance.
- Clouds are a separate system.
