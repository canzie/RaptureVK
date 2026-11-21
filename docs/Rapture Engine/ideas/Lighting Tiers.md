# Lighting Tiers

Where lighting goes after [[DDGI]]: a set of tiers from fully baked to ray traced at the limit of current research, sharing everything that is not the GI technique itself. Grew out of the DDGI flicker hunt in [[DDGI Noise and Convergence]] and the broader picture in [[Rendering and GI Roadmap]].

> Idea / direction. Nothing here is built yet, and none of it is final.

## Why not keep pushing DDGI

DDGI was polished on and off for months and there was always another artefact. The last round showed why: in Sponza's galleries the light comes from a few small sunlit patches seen through the arches, and every probe's estimate depends on how many of its rays happen to land on them. Hysteresis only trades flicker for lag, more rays stopped helping past about 512, and none of relocation, classification or the large-change hysteresis drop was the cause. A flashlight in a dark room would be worse still: a smaller, brighter source that also moves.

DDGI used as the final image is soft, leaky, has no reflections and flickers under high-contrast light. It is good at a different job: a world-space cache of diffuse light that can be read from anywhere (ray hits, volumetrics, transparents, off-screen) at a fixed cost. Modern ray traced engines use a probe or hash cache exactly that way, behind a per-pixel pass, where its noise is only ever seen through a bounce.

## The range of games

From low-poly and stylised with fully baked lighting, to real-life recreations at the highest fidelity real-time research allows. The game most likely to actually be made sits in between: stylised geometry with realistic light, meaning a good sky, good tone and a dynamic sun. PC first; consoles are not ruled out.

## Tiers

| Tier | Hardware | Diffuse | Reflections | Shadows |
|---|---|---|---|---|
| Baked | any GPU | lightmaps for static geometry, a baked probe grid for dynamic objects, a few slowly updated dynamic probes where needed | baked cubemaps plus screen space | shadow maps |
| Ray traced | ray tracing hardware | per-pixel ray traced final gather, reused across pixels and time ([[ReSTIR]]), denoised, backed by a world-space [[Radiance Cache]] for later bounces | ray traced, with screen space where it is cheaper | ray traced |

A dynamic tier without ray tracing hardware (signed distance fields or voxels, like Godot's SDFGI or Lumen's software mode) is the hardest of all to build and is left out until a game needs it. Consoles would mostly run the baked tier, possibly with ray traced reflections or shadows on top.

Tiers are options a project picks, not a fixed ladder: a stylised game can ship baked, a realism scene can run fully ray traced.

## What every tier shares

- Physical light units and materials. The sun at π is the "a white surface facing it gives back its albedo" normalisation, sky intensity sets the sun to sky ratio (about 5-7:1 on the ground at clear noon), exposure is set on the camera.
- Sky and atmosphere, [[Auto Exposure]], tone mapping. These carry most of the look for the stylised-with-realistic-light middle ground.
- One GI interface for the lighting pass: diffuse light and reflection at a pixel, whichever tier answers.

## Reference path tracer

A progressive path tracer that runs in the engine: noisy at first, converging over time, still possible to fly around in and stop where a nice frame is wanted. It serves three purposes at once:

- The realism hobby: offline quality renders of any scene.
- The light baker for the baked tier, so there is no separate baker to maintain.
- Ground truth. Every real-time technique gets measured against it instead of judged by eye.

## Order

1. Finish the remaining editor panels.
2. Clean up the GI code so it is no longer shaped around DDGI: a generic GI layer that DDGI is one implementation of.
3. The reference path tracer.
4. A deep study of [[Kajiya]]: what it does and does not do, until it is understood properly.
5. Newer research since kajiya, then build the ray traced tier as far as it can go.

The baked tier slots in once the path tracer can bake.

## Still open

- Whether DDGI survives as the radiance cache behind the per-pixel pass, or a spatially hashed cache (such as NVIDIA's SHaRC) replaces it.
- Whether [[Reflective Shadow Maps]] are worth it in the baked tier's dynamic probes, for sun and flashlight bounce without ray tracing.
- How the baked tier handles a moving sun: time-of-day baking, relighting baked probes, or accepting a static sun.
- Which GPUs count as which tier, and whether the engine picks automatically or the project decides.
- Whether a local tone mapper (exposure fusion or a bilateral grid) is needed on top of auto exposure for interiors next to bright exteriors.
