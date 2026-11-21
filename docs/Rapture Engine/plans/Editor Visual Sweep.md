# Editor Visual Sweep

A pass over how the editor looks and behaves, worked out in HTML mocks first because every change in the editor costs a rebuild. The palette in `assets/themes/theme.ams` stays; what changes is sizing, structure, states and a handful of added colours. Functional work (thumbnails, asset picker, persistence) lives in [[Editor UX Polish Roadmap]].

## Working in the mocks

The mocks live in `tools/ui_mock/`:

- `world_editor.html` reproduces the editor as it is (`current`), measured off a 1080p capture. Kept as the baseline to compare against.
- `world_editor_sweep.html` is the World Editor proposal. Query options: `open=menu,popup` (Render menu, command popup), `browser=floating|docked`, `tooltip=button|asset`, `state=playing,unsaved`.
- `material_editor.html` is the Material Editor proposal. Query options: `menu=add` (add node popup), `canvas=grid|plus` (background alternatives), `select=<node>`.
- `mesh_editor.html` (static mesh), `skeletal_mesh_editor.html`, `skeleton_editor.html`, `texture_viewer.html`, `scripting.html`, `module_editor.html` and `animation_editor.html` are the first passes of the other workspaces, each with its query options listed at the top of the file. They share `workspace_chrome.js` (menu bar, workspace tabs, hotbar, panels, bottom bar, orientation gizmo, a property sheet builder), the skeleton mocks also `skeleton_tree.js` (the Skeleton panel) and `mannequin_stage.js` (the viewport figure), and `workspaces.css` on top of `sweep.css`.
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

## Other workspaces: first pass

### What a workspace shows

The hotbar carries a status note only where there is a real state to report (compiled or failed, a runtime error). Facts about the asset (section count, triangles, resolution) are not a status; they already sit in Details and the viewport read out.


A workspace is opened from one asset, but what it lets you reach depends on the kind of work. Some are strictly that one asset: static mesh, skeletal mesh, skeleton, texture, material, module. Some show the whole set the asset belongs to: Scripting shows every script in the project, and the animation workspace shows every clip on the skeleton of the clip that was opened. The set is a query, never a folder, so the assets can live wherever the project keeps them.

### Static Mesh

Viewport with a Lit menu and a Show menu (grid, bounds, wireframe, section colours) and a stats read out (triangles, vertices, bounds). Details: material slots, one per section, as asset pickers; geometry facts; import settings. Hovering a slot outlines its section in the viewport. Hotbar: Save, Reimport.

Scale and up axis are not import settings to reimport with. The Transform section edits them with a live preview on the mesh, and Save bakes them into the asset. What the mesh was imported with lives in its metadata (a `MeshImportConfig` in `AssetImportConfigVariant`, which `AssetCodec` does not encode yet): the Import section shows it as text, the Transform section starts from it, and Save writes the new values back to it and updates every scene object using that mesh asset. Today the edited values sit in a struct the workspace owns only so the section can reach the preview mesh; that goes once the section previews through the scene and Save asks the panel to apply.

### Skeletal Mesh

Built like the static mesh workspace, plus what skinning adds on top: the skeleton it is bound to, influences per vertex, a joints overlay in the viewport, maybe a read only joint list. The hierarchy is not edited here, that belongs to the skeleton.

Baking to a static mesh happens in two places sharing one engine function. The asset workspace bakes the pose it previews (the bind pose, or a picked animation frame). A `SkeletalMesh3D` in a world bakes the live pose it is drawn with, from its context menu, since only the scene object has a live pose.

### Skeleton

About the joints, not a mesh: joint tree, a viewport showing the joints with a rotate gizmo on the selected one, joint details (parent, rest translation, rotation, scale). The joints are shown on a preview mesh so they are not floating in space; `ASkeleton` already stores its preview meshes, so Details lists them with add and remove and the viewport shows all of them at once, posed by the same skeleton, so the pieces of a modular character are seen together. With no preview mesh the viewport shows the raw joints alone.

### Animation

A full workspace of its own, not a mode of the skeleton one. Panels: the skeleton's joint tree, viewport, Timeline, an Animations list (every clip on the opened clip's skeleton, picking one switches the clip shown), Details. The preview mesh is picked from the skeleton's preview meshes, the same way the skeleton workspace does it. In the preview the character animates in place, root motion is shown as a read out and not applied (see [[Animation System]]).

Timeline:

- Transport row: the current frame as a field with the length and time beside it on the left, previous key, play and next key centred, loop on the right. No playback rate slider.
- Channels: one collapsible group per joint, its row showing every key the joint has. Opening a group shows a row per animated channel (Rotation X, Y, Z and so on) with its axis colour and its value at the playhead, like Blender's dope sheet. A filter field above the channel names.
- An Events lane on top (footstep markers and the like).
- Details shows the clip (skeleton, length, frame rate, loops), a Selected Key section (channel, frame, value, interpolation) when a key is selected, and the preview mesh.

`animation_editor.html` still has the earlier version that switched between skeleton, skeletal mesh and animation in one workspace. The skeleton and skeletal mesh halves are split out into `skeleton_editor.html` and `skeletal_mesh_editor.html`; the animation half is still to be split into its own mock.

### Texture

Canvas with the image on a checkerboard, channel toggles (R, G, B, A), a mip picker, Fit, 1:1 and Tile, a read out of the texel under the cursor, a column of mip thumbnails when the texture has mips. Details: resolution, format, mips, GPU memory, size on disk; sampling; import settings (colour space, compression, generate mips, flip green on normal maps). Hotbar: Save, Reimport.

### Scripting

A code editor over the project's scripts, not one file:

- Scripts panel: every folder under `content` shows, scripts as the only files. Folders with no scripts below them are dimmed and start collapsed, so a new folder is still reachable. Right click on a folder: New Script, New Folder, Rename, Show in Content Browser. New Script in the hotbar creates in the selected folder. Searching narrows the tree to matching scripts and their folders.
- Editor: tabs per open script, Lua highlighting, current line, minimap, a status line (problems, line and column, indentation, line endings, language). Find bar, member completion with a doc line, runtime errors pinned to their line.
- Output and Problems tabs below the editor.
- Outline panel with collapsible Symbols (locals, functions, connected events) and Run by sections. Run by lists the scene objects running the script, name on the left and class on the right; which world or module one sits in goes in a hover tooltip where it matters.

### Module

Viewport with a Script tab, Scene Objects tree, Properties. The root is marked in the tree and cannot be deleted. Adding objects: Add in the hotbar, or right click a row for Add child and Delete (what `SceneObjectTreePanel` builds today). Components have no place in the tree, so they are added from an Add Component button beside the Properties search and removed from the menu on their section header.

Add Component opens a window of its own (`add_component.html`), moved by dragging its title bar: a search field and a row per component (icon, name, one line description, attached ones greyed out). A click on a row adds it; a small button at the row's end opens a longer description under the row, so reading more never costs an extra click to add. It closes once a component is added, on Escape, and on a click outside it, since it adds to the object that was selected when it opened. Only component sections carry a Component tag and that menu; the object's own sections (Transform, Static Mesh) cannot be removed. Tree rows show small icons for the components attached.

### Project layout

The browser shows `content` only; `blobs` and `.cache` are its siblings and never shown. Inside `content` nothing is enforced. The suggestion is feature folders rather than type folders: a character's skeleton, meshes, clips, materials and textures together, and an import of a rigged glTF lands as one folder holding all of them. Type based views come from queries (the Scripting workspace, the Animations list), not from folders.

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

- The Events lane stays in principle, what an event holds and how it is authored is not worked out.

- Whether the Object Mode dropdown has a control today, or is new.
- The Outliner search field is new, not a restyle.
- Whether the Render menu's regrouping and shorter labels match how its options actually relate.
- A focused or hovered panel indicator: tried an outline, dropped.
- Canvas background: dots preferred, grid kept as the alternative.
