# Curve Editor Widget

A reusable **retained-mode Amethyst widget** for editing curves by dragging control points on a graph. First consumer is the terrain spline system, but it's generic: material remaps, tonemapping, falloff, animation easing.

> Idea / design. Look is **TBD** — Claude design will give a template to work from for inspo.

## Key points

- **It's a widget, like the [[gizmo]] — not a `UIObject` component.** A self-contained class in `components/widgets/` that owns its `Canvas` and draws itself. Unlike the gizmo it holds its own curve state and needs no per-frame `update(params)` driving call; it just reports edits via an `onChanged` callback. See [[Amethyst UI Library]].
- **Stackable, like Blender's graph editor.** Multiple curves live in one editor at once — overlaid on shared axes, each a channel you can show/hide and pick as active. The terrain panel stacks continentalness / erosion / peaks-valleys; an animation view stacks transform channels. So the data model is a *list of curves*, not one curve.
- **Maps onto existing `TerrainSpline`** (`vector<vec2>` points) with no conversion.

## Data model

```cpp
enum class CurveInterpolation { LINEAR, SMOOTH /* monotone cubic */ };

struct Curve {
    std::vector<glm::vec2> points;   // x = input, y = output
    CurveInterpolation interp;
    bool visible;
    // color/label = theme + caller
};
```

Widget holds `std::vector<Curve>` + an active index. Per-editor: `domainX` (e.g. `{-1,1}`), `rangeY` (e.g. `{0,1}`), `lockEndpointsX`, `onChanged`.

## Behavior

- Drag point (clamp to domain/range, keep sorted); click empty to add; right-click/Delete to remove (min 2).
- Edits hit only the active curve; others are dimmed reference lines.
- Every edit marks dirty + fires `onChanged`.
- Interpolation is cosmetic to the GPU: terrain **bakes** the curve (`bakeSplineCurves`), so SMOOTH costs nothing on the shader side.

## Optional overlays

- **Noise histogram** behind the curve — Perlin clusters at the midpoint, so it shows which part of the curve actually gets sampled. Teaches redistribution, stops wasted tuning at the extremes.
- **Baked-curve reference line** read back from the texture, so what you see == what the GPU samples.

## Terrain wiring

```
onChanged: activeCurve.points → TerrainSpline.points → terrain.bakeSplineCurves()
```

One stacked editor for all three noise categories. See [[Terrain Editor]]. Also a natural control type for the [[Procedural Texture and Shader Editor]].
