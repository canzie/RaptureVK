# Procedural Texture & Shader Editor

A **workspace** (not a single panel) for authoring procedural/compute shaders: pick a shader, get auto-generated controls for its parameters, tweak against a live preview, and see/edit the source — wired to hot reload so saving updates the controls and preview in place.

This existed in the old ImGui editor ("give it a shader, it gives you stuff to tweak" — procedural textures, atmospheric scattering) and hasn't been reimplemented since the Amethyst move.

> Idea / design. Look is **TBD** — Claude design will give a template to work from for inspo.

## A workspace of panels

Like Blender, this is a workspace composed of dockable panels, not one monolithic view:

- **Preview** — the output texture; zoom/pan, channel toggles (R/G/B/A, grayscale).
- **Source** — the shader file, shown in-editor (read-only first, editable later).
- **Inputs** — the auto-generated parameter list/controls.
- (room for more: histogram, output settings, presets)

## Mostly wiring — the hard parts exist

`shaders/ShaderReflections.h` already gives:
- `extractDetailedPushConstants(spirv)` → every member (name, type, base type, offset, size).
- `parsePushConstantAnnotations` / `applyPushConstantAnnotations` → GLSL comment metadata: `@range(min,max)`, `@default(...)`, `@name("...")`, `@hidden`, `@color`.

So a shader describes its own UI. `generators/textures/ProceduralTextures.h` already runs the compute generators — generalize its hardcoded per-generator structs into one **dispatch-by-reflection** path so any procedural shader (incl. future user ones) gets controls for free.

## Core loop

```
.spv → reflect (members + annotations) → parameter model → Inputs panel
     → push constants → dispatch compute → output texture → Preview
```

Control mapping: float+`@range` → slider; int/uint → stepper (seed, octaves); vec3/4+`@color` → color picker; `@hidden` → skip; (future) `@curve` → embed [[Curve Editor Widget]].

## Hot reload (the differentiator)

On save/recompile: re-reflect, re-parse annotations, **re-merge** with the live model (keep values whose name still exists, add new at `@default`, drop removed), rebuild changed controls, re-dispatch. You edit the shader and its tweakables + preview update without restart. Phase 1 can be read-only source reacting to external saves — most of the value, no text editor needed.

## Relationship to terrain

The terrain noise maps are these generators. Author/tune a noise map here, hand it to the [[Terrain Editor]] as a continentalness / erosion / peaks-valleys input; the [[Curve Editor Widget]] is shared by both.
