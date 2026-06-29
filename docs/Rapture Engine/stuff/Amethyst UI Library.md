# Amethyst UI Library

Amethyst is a **retained-mode C++ UI library** used in the Rapture Editor. It lives at `Engine/vendor/Amethyst/`. It is under active development — features are added as needed.

Unlike ImGui (immediate-mode), Amethyst builds a persistent widget tree that persists between frames. Properties are set once and the library re-computes layout automatically on dirty.

---

## Core Concepts

### Instance Hierarchy (the DOM equivalent)

Every UI object is an `Instance` — the base class for all nodes in the UI tree.

```cpp
Instance
├── UIBase2D         — anything drawable (2D)
│   ├── UILayer      — root containers (Window, PanelLayer, OverlayLayer, DockingLayer)
│   └── UIObject     — interactive widgets (Frame, Button, Label, etc.)
```

Key `Instance` API:
- `add<T>()` — create and add a child of type T
- `addChild()`, `removeChild()`, `reparent()`
- `findFirstChild()`, `findFirstDescendant()`, `findFirstChildOfClass()`
- `markDirty()` — invalidate for re-layout/re-draw

### UIObject (base for all interactive widgets)

All widgets inherit from `UIObject`. It provides layout, styling, and input properties.

**Layout:**

| Property | Type | CSS Equivalent |
|----------|------|----------------|
| `position` | `UDim2` | `left` + `top` |
| `size` | `UDim2` | `width` + `height` |
| `padding` | `UDim4` | `padding` |
| `anchorPoint` | `glm::vec2` (0–1) | `transform-origin` |
| `automaticSize` | `AutomaticSize` | `width: fit-content` |

**Visual:**

| Property | CSS Equivalent |
|----------|----------------|
| `backgroundColor` | `background-color` |
| `backgroundTransparency` | `opacity` |
| `borderColor`, `borderPixelSize`, `borderMode` | `border` |
| `cornerRadius` | `border-radius` |
| `clipsDescendants` | `overflow: hidden` |
| `zIndex`, `zindexBehavior` | `z-index` |
| `rotation` | `transform: rotate()` |
| `visible` | `visibility` |

**Interaction:** `guiState` (IDLE, HOVER, PRESS, NON_INTERACTABLE), `interactable`

---

## Unified Dimension System (UDim)

Amethyst uses `UDim` for all sizes and positions — a `scale * parent + offset` model identical to how CSS percentages + px work.

```cpp
UDim::fromScale(0.5f)            // 50% of parent  →  CSS: width: 50%
UDim::fromOffset(100.0f)         // 100 px          →  CSS: width: 100px
UDim(0.5f, 10.0f)                // 50% + 10px      →  CSS: calc(50% + 10px)

UDim2::fromScale(1.0f, 1.0f)     // 100% × 100%
UDim2::fromOffset(200.0f, 100.0f)// 200px × 100px
```

`UDim4` is the rect variant used for `padding` (top, right, bottom, left).

---

## Widget Reference

### Containers

| Widget | CSS/HTML Analogue | Notes |
|--------|------------------|-------|
| `Frame` | `<div>` | Basic rectangular container, no layout logic of its own |
| `ScrollingFrame` | `overflow: scroll` | Scrollable viewport; `scrollAxis`, `canvasSize`, `scrollBarVisibility` |
| `Canvas` | `<canvas>` | Immediate-mode drawing surface, cleared each frame |
| `DockingLayer` | — | BSP-based docking (split panes), save/restore via `saveConfig()` |
| `OverlayLayer` | `position: fixed` | For dropdowns, modals |

**Canvas primitives:** `drawLine`, `drawTriangleFilled/Stroke`, `drawQuadFilled/Stroke`, `drawCircleFilled/Stroke`, `drawEllipseStroke`, `drawText`

### Labels (non-interactive)

| Widget | Analogue |
|--------|----------|
| `TextLabel` | `<p>` / `<span>` |
| `ImageLabel` | `<img>` |

`TextLabel` properties: `text`, `fontSize`, `fontFamily`, `textColor`, `textXAlignment`, `textYAlignment`, `textWrapped`, `textTruncate`, `lineHeight`, `strokeThickness`, `richText`

`ImageLabel` properties: `image` (AmTextureId), `imageColor`, `scaleType` (STRETCH, TILE, FIT, CROP), `tileSize`

### Buttons

| Widget | Notes |
|--------|-------|
| `TextButton` | Text + full styling, `autoButtonColor` for hover/press tint |
| `ImageButton` | Image source, optional `hoverImage` |
| `InvisibleButton` | Hit area only, no visuals — useful for drag handles |

Callback pattern (all on `UIButton`):
```cpp
button->onMouseButton1ClickCb = [](){ /* ... */ };
button->onMouseEnterCb = [](){ /* ... */ };
```

### Form Controls

| Widget | Analogue | Key Properties |
|--------|----------|----------------|
| `TextInput` | `<input>` / `<textarea>` | `getText()`, `setText()`, `multiline`, `placeholderText`, `maxLength`, `readOnly` |
| `Checkbox` | `<input type="checkbox">` | `valueRef: bool*`, `onValueChanged` |
| `SliderFloat` | `<input type="range">` | `valueRef: float*`, `min`, `max`, `speed` |
| `SliderInt` | same | integer variant |
| `SliderVec2/3` | — | vector value variant |
| `Dropdown` | `<select>` | `optionsRef: vector<string>*`, `selectedIndexRef: int*`, `popupDirection` |
| `RadioButton` | `<input type="radio">` | (partially implemented) |

### Complex Components

**`TabBar`** — tabbed interface
- `select(index)`, `getSelectedContent()`, `getTabCount()`
- `tabPosition` (TOP/BOTTOM/LEFT/RIGHT), `closeable`, `persistLayout`
- Torn-off tab callbacks for detachable panels

**`Table`** — grid with fixed columns
- Children auto-arranged into rows: `row = childIndex / numCols`
- `columnWeights` controls relative widths; `rowHeight` can be auto

**`TreeView`** — hierarchical table (extends Table)
- `beginRow(parentRow)` / `endRow()` to build rows
- `expand()`, `collapse()`, `expandAll()`, `toggle()`
- `forEachVisibleRow()` for iteration
- Properties: `indentPerLevel`, `showDisclosureTriangles`, `rowHoverColor`, `rowSelectedColor`

**`MenuBar`** — top-of-window menu (partially implemented)

---

## Layout Extensions

Layouts attach to a container as extensions, not subclasses. CSS analogy: think of them as applying `display: flex` or `display: grid` to a div.

```cpp
auto layout = container->addExtension<UIListLayout>();
// or
auto grid = container->addExtension<UIGridLayout>();
```

### UIListLayout — Flexbox equivalent

| Property | CSS Equivalent |
|----------|----------------|
| `fillDirection` | `flex-direction` |
| `horizontalAlignment` / `verticalAlignment` | `align-items`, `justify-content` |
| `innerPadding` | `gap` |
| `horizontalFlex` / `verticalFlex` | `justify-content` (FILL, SPACE_AROUND, SPACE_BETWEEN, SPACE_EVENLY) |
| `wraps` | `flex-wrap` |
| `itemLineAlignment` | `align-items` per line |
| `sortOrder` | `order` (by name or layoutOrder) |

### UIGridLayout — CSS Grid equivalent

| Property | CSS Equivalent |
|----------|----------------|
| `cellSize` | `grid-template-columns` / `rows` |
| `cellPadding` | `gap` |
| `fillDirectionMaxCells` | `grid-template-columns: repeat(N, ...)` |
| `startCorner` | `direction` + writing mode |

### Constraints

| Extension | Effect |
|-----------|--------|
| `UISizeConstraint` | `min-width`, `max-width`, `min-height`, `max-height` |
| `UIAspectRatioConstraint` | `aspect-ratio` |
| `UITextSizeConstraint` | Clamp `font-size` |
| `UIDragDetector` | Draggable widget; modes: FREE, HORIZONTAL, VERTICAL, SOFT_HORIZONTAL, SOFT_VERTICAL |

---

## Styling System

`Style` is a singleton that stores typed property values keyed by `(StyleProperty, ComponentType)`. It cascades up a type hierarchy, similar to CSS specificity:

```
TEXT_BUTTON → UI_BUTTON → UI_OBJECT → (compiled default)
```

So a `TEXT_BUTTON` without an explicit `FONT_SIZE` inherits it from `UI_BUTTON`, then `UI_OBJECT`.

```cpp
Style &style = Style::instance();
Color4 c = style.get<Color4>(StyleProperty::TEXT_COLOR, ComponentType::TEXT_BUTTON);
style.set(StyleProperty::FONT_SIZE, ComponentType::UI_OBJECT, 14.0f);
```

Styles can be loaded from a file: `Style::load(path)`.

**82 style properties** cover: background, border, padding, text, scrollbar, table/tree, slider, checkbox, label, highlight, tab.

---

## Color System

`Color3` / `Color4` automatically convert sRGB → linear on construction.

```cpp
Color3::fromHex(0xFF5733)
Color3::fromRgb(255, 87, 51)
Color4(1.0f, 0.0f, 0.0f, 0.5f)   // red, 50% alpha
```

---

## Event Handling

Input flows from `Window` → hit test → target `UIObject`.

Virtual methods to override:
```cpp
onMouseEnter(), onMouseLeave()
onMouseMoved(x, y)
onMouseButton1Click(), onMouseButton1Down(x, y), onMouseButton1Up(x, y)
onMouseButton2Click(), onMouseButton2Down(x, y), onMouseButton2Up(x, y)
onMouseScrollUp(), onMouseScrollDown()   // return bool to consume
```

Mouse capture for drag operations:
```cpp
window->captureMouse(myWidget);
window->releaseMouse(myWidget);
```

Keyboard input goes to a focused widget (handled internally by `TextInput`, etc.).

---

## Rendering Architecture

Amethyst is renderer-agnostic. Every visual primitive is converted to a 64-byte `InstanceData` struct and written into a `GeometryRegistry` (one per `UILayer`). The backend is responsible for uploading dirty instances to the GPU and drawing them.

### InstanceData (GPU primitive)

```cpp
struct InstanceData {
    glm::vec2 translation;       // top-left position
    glm::vec2 scale;             // width, height
    glm::vec4 clipRect;          // scissor (minX, minY, maxX, maxY)
    uint32_t  fillColor;         // packed RGBA
    uint32_t  borderColor;       // packed RGBA
    uint32_t  shapeData[4];      // UV / triangle verts / line endpoints
    uint32_t  rotationBorderThickness;
    uint32_t  cornerPrimitiveMode;  // cornerRadius | primitiveType | borderMode
    uint32_t  textureId;         // bindless handle
    int32_t   zIndex;
    uint32_t  flags;             // INSTANCE_FLAG_VISIBLE
};
```

### Primitive Types

`PRIMITIVE_RECT`, `PRIMITIVE_CIRCLE`, `PRIMITIVE_TRIANGLE`, `PRIMITIVE_LINE`, `PRIMITIVE_TEXT`, `PRIMITIVE_CANVAS_LINE/TRI/QUAD/CIRCLE/ELLIPSE`

### Backend Integration (per frame)

```cpp
for (auto* registry : GeometryRegistry::getRegistries()) {
    auto dirty = registry->consumeDirtyIndices();
    const auto& instances = registry->getAllocations();
    for (auto idx : dirty) {
        uploadToGpu(instances[idx]);
    }
    // sort by zIndex, set clip rect, dispatch draw calls
}
```

Glyph atlas: check `glyphAtlas->isDirty()`, re-upload the 1024×1024 grayscale atlas texture when true.

---

## Text Rendering Pipeline

1. `FontLoader` (FreeType) rasterizes glyphs on demand
2. `GlyphAtlas` packs them into a 1024×1024 atlas (skyline packer)
3. `TextProcessor::layoutTextAtlas()` produces a list of `InstanceData` (one per glyph)
4. Backend renders each glyph as `PRIMITIVE_TEXT` using the atlas texture

`TextLayoutParams` mirrors CSS text properties: `fontSize`, `color`, `letterSpacing`, `lineHeight`, `xAlign`, `yAlign`, `truncate`, `wrap`, `strokeThickness`.

---

## Persistence

`TabBar` and `DockingLayer` support save/restore of layout state:
```cpp
tabBar->saveConfig();
tabBar->applyConfig();
dockingLayer->saveConfig();
dockingLayer->applyConfig();
```

Backed by `LayoutConfig` (singleton), which reads/writes a config file.

---

## Known Gaps / TODO

From `Engine/vendor/Amethyst/todo.md`:
- Corner resizing in docking layer
- More docking split ratios (currently 50/50 only)
- Text allocation improvements (max size buffers)
- Tweening / animation system
- Gradient extension
- `RadioButton` not fully implemented
- `MenuBar` partially implemented

---

## See Also

- [[SceneRenderData Implementation]] — how the Rapture backend plugs into Amethyst's GeometryRegistry
- `Engine/vendor/Amethyst/libamethyst/` — full source
- `Engine/vendor/Amethyst/backends/` — reference backend implementations
