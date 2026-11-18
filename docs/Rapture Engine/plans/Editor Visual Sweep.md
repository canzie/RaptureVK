# Editor Visual Sweep

A pass over how the editor looks and behaves, worked out in HTML mocks first because every change in the editor costs a rebuild. The palette in `assets/themes/theme.ams` stays; what changes is sizing, structure, states and a handful of added colours. Functional work (thumbnails, asset picker, persistence) lives in [[Editor UX Polish Roadmap]].

## Working in the mocks

The mocks live in `tools/ui_mock/`:

- `world_editor.html` reproduces the editor as it is (`current`), measured off a 1080p capture. Kept as the baseline to compare against.
- `world_editor_sweep.html` is the World Editor proposal. Query options: `open=menu,popup` (Render menu, command popup), `browser=floating|docked`, `tooltip=button|asset`, `state=playing,unsaved`.
- `material_editor.html` is the Material Editor proposal. Query options: `menu=add` (add node popup), `canvas=grid|plus` (background alternatives), `select=<node>`.
- `sweep.css` holds the styles both proposals share. `icons.js` is generated from `Editor/src/Icons.h` by `extract_icons.py`.
- `render.sh <page> "<query>"` renders a page at 1908x1048 with headless Firefox into `renders/`, next to a side by side with `reference/current.png`. `reference/` and `renders/` are not committed.

Hover states are live when a mock is opened in a browser, written the way they would become `:hover` rules in the theme.

## Decisions that hold everywhere

- Text: 13px for labels and values, 12px secondary, 11px captions. Section titles bold. Secondary text (types, descriptions) in `text-3`. Monospace (IBM Plex Mono, already in `assets/fonts`) for the command line, paths, ids and stats.
- Controls: 4px corner radius, a fill inset 1px from a subtle border, a darker border than now, borders lighten on hover. Slider fill `#383838` so bright value text reads against it. Single value fields (sliders, dropdowns) have a fixed width instead of filling the row.
- Island bars: sections divided by full height, window coloured separators, the pattern the bottom bar already had. Used by the workspace bar, the hotbar, the content browser toolbar, the bottom bar and table headers.
- Tabs: no accent. The tab in front takes the body colour, the others sit darker with dimmed text. Panel tabs a constant 160px, close button clear of the label, ellipsis instead of clipping.
- Menus and popups: border and 6px radius, no drop shadow until Amethyst can draw one. Section labels on the left with a line to the edge. Radios without a border, the black input fill when off and solid white when on. Checkboxes 16 to 18px with an accent check.
- Colour carries meaning, once: per asset type colours come from `Asset_colorForType` and are reused for workspace tab icons. XYZ fields carry red, green and blue bars. Outliner icons stay neutral.
- Tooltips sit next to the cursor with a gap, say what a control does rather than repeat its label, and show the shortcut.
- Hover effects on everything interactive.

## World Editor: mock done

- Workspace bar: tabs as islands, 36px tall, 16px icons in the asset type colour (full in front and on hover, dimmed otherwise). Unsaved changes show as a dot in place of the close button until hovered.
- Hotbar: one island per control: Object Mode dropdown, Add as the raised primary button (green plus, bold), Save World, then larger play controls in their own group.
- Play state: stop button, viewport outline and a note in bright red (`#F04848`): "Playing, changes made now are undone when you stop".
- Viewport: image inside the panel with a 6px margin and 3px radius. Transform and view mode in one floating toolbar. Orientation gizmo top right, positive axes a line and a labelled ball, negative axes a ring, Y up.
- Render menu: grouped into View mode, Features and Probe debug.
- Outliner: search field, a header row with full height separators, a real tree (expanders, 16px per level, a guide per level), zebra continued to the bottom of the panel.
- Properties: slimmer search, labels indented under bold component headers, square colour swatch with the hex beside it, reset to default on changed rows in a slot every row keeps.
- Bottom bar: unchanged layout, command field styled like every other field with even spacing around it, frame time, fps and VRAM in their own island on the right.
- Command popup: completions with descriptions above the command line.

## Content Browser: mock done

- Same layout docked or floating. Floating sits 6px above the bottom bar, left aligned with its button, the button showing it open.
- Toolbar as an island bar: Add, Import, back, forward, up, breadcrumb, then search, Dock (floating only) and settings, with larger icons.
- Sidebar: Project and Recent as section labels, Recent directly below the tree.
- Cards as they are now, footer part of the card, smaller (124 by 150).
- Asset tooltip: preview, name, type chip, then a short list of facts (location, size on disk, id).

## Material Editor: first pass

- Hotbar: Compile as the primary button, Save, a compile status note.
- Panels: Preview (floating toolbar for preview mesh and view mode, no orientation gizmo), Details showing the selected node in the Properties style, Surface Graph.
- Graph: dotted canvas inside a 6px margin, breadcrumb top left, zoom read-out and a frame all button top right.
- Nodes: rounded dark bodies, header tinted with the category colour across its whole width, bold titles, pins coloured by type with unconnected pins as rings, inline fields for unconnected values, mode selectors as dropdowns, square previews with even padding, accent outline on the selected node.
- Wires: bezier curves in their pin's type colour, drawn under the nodes.
- Add node popup at the right click point: search, results grouped by category with a colour square and a one line description.

## Other workspaces: not started

Scripting, Static Mesh, Skeletal Mesh, Skeleton and Module Editor.

## Order of work

World Editor first, then the Content Browser. Within that, the steps that only change sizes, colours and states come before anything that changes what a panel is made of.

### 1. Sizing and styling

- [ ] Text: 13px labels and values, 12px secondary, 11px captions, bold section titles, secondary text in `text-3`, IBM Plex Mono for the command line, paths, ids and stats.
- [ ] Controls: 4px radius, darker border that lightens on hover, slider fill `#383838`.
- [ ] Single value fields (sliders, dropdowns) at a fixed width instead of filling the row.
- [ ] Tabs: no accent, the front tab in the body colour, the others darker with dimmed text.
- [ ] Panel tabs a constant 160px, close button clear of the label, ellipsis instead of clipping.
- [ ] Menus and popups: border and 6px radius.
- [ ] Radios without a border (input fill off, solid white on), checkboxes 16 to 18px with an accent check.
- [ ] Workspace bar 36px tall, 16px icons.
- [ ] Viewport image inside a 6px margin with a 3px radius.
- [ ] Bottom bar: command field styled like other fields with even spacing around it.
- [ ] Properties: slimmer search field.
- [ ] Hover states on every interactive control.

### 2. Structure

- [ ] Island bars: workspace bar, hotbar, bottom bar (frame time, fps and VRAM in their own island), table headers.
- [ ] Hotbar: Object Mode dropdown, Add as the primary button, Save World, play controls in their own group.
- [ ] Play state: stop button, red viewport outline and note.
- [ ] Workspace tabs: icons in the asset type colour, unsaved dot in place of the close button until hovered.
- [ ] Viewport: transform and view mode in one floating toolbar, orientation gizmo, overlays chosen at creation.
- [ ] Render menu grouped into View mode, Features and Probe debug.
- [ ] Menu section labels with a line to the edge.
- [ ] Outliner: search field, header row with full height separators, tree guides at 16px per level, zebra to the bottom.
- [ ] Properties: labels indented under bold component headers, colour swatch with the hex beside it, reset to default slot on every row, XYZ fields with red, green and blue bars.

### 3. Tooltips

- [ ] Placed next to the cursor with a gap.
- [ ] Text says what the control does and shows its shortcut, on every hotbar, toolbar and menu control.

### 4. Content Browser

- [ ] Toolbar as an island bar: Add, Import, back, forward, up, breadcrumb, search, Dock (floating only), settings.
- [ ] Sidebar: Project and Recent as section labels, Recent directly below the tree.
- [ ] Rooted at `content`, `blobs` and `.cache` hidden.
- [ ] Cards 124 by 150 with the footer part of the card.
- [ ] Floating placement 6px above the bottom bar, aligned with its button, the button showing it open.
- [ ] Asset tooltip: preview, name, type chip, location, size on disk, id.

## Needed in Amethyst

- A pattern primitive for canvas backgrounds: one quad, the dot, grid or cross pattern computed in `ui.fs.glsl` from position, spacing and offset, never one element per dot.
- Drop shadows for floating surfaces, once the border only version has been lived with.
- The 1px inset between a field's border and its fill.
- Ellipsis on tab labels (`text-truncate` exists, check it reaches tabs).
- Section labels with a trailing line in context menus.

## Needed in the editor

- `ViewportPanel` takes which overlays it shows (orientation gizmo, toolbar) at creation, so previews can leave the gizmo out.
- Workspace tab icons in the asset type colour of what each workspace edits.
- Content browser rooted at `content`, project internals (`blobs`, `.cache`) hidden.
- Recent placed below the tree instead of reserving the leftover height.

## Not mocked, done straight in the editor

Drag and drop feedback, inline rename, number scrubbing, mixed values for multi selection, asset reference fields, empty, loading and error states, notifications, keyboard focus, docking feedback.

## Still open

- Whether the Object Mode dropdown has a control today, or is new.
- The Outliner search field is new, not a restyle.
- Whether the Render menu's regrouping and shorter labels match how its options actually relate.
- A focused or hovered panel indicator: tried an outline, dropped.
- Canvas background: dots preferred, grid kept as the alternative.
