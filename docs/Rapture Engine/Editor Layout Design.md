# Editor Layout Design

## Concept

The Rapture editor uses a **workspace tab system** inspired by UE5, where each tab is a named **Workspace** — a self-contained editor environment. The terminology comes from Blender (Layout, Modeling, Sculpting, etc. are Blender workspaces).

## Planned Workspaces

| Workspace | Purpose | Status |
|-----------|---------|--------|
| Level Editor | 3D scene editing — Outliner, Viewport, Properties | In progress |
| Material Editor | Node-based material authoring | Planned |
| Animation | Skeletal animation / timeline | Planned |
| Blueprint / Script | Visual scripting or text editing | Planned |

## Window Structure

```
┌──────────────────────────────────────────────────────────────────┐
│  Main Menu Bar   (File / Edit / View / Window / Help / ...)      │
├──────────────────────────────────────────────────────────────────┤
│  [Level Editor] [Material Editor] [Animation] [+]               │  ← Workspace tabs
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│   Workspace Header Bar  (tools / modes for this workspace)       │
│                                                                   │
│   Workspace Content  (docking layout — panels, viewport, etc.)   │
│                                                                   │
├──────────────────────────────────────────────────────────────────┤
│  Bottom Bar  (Content Browser drawer + output / status strip)    │
└──────────────────────────────────────────────────────────────────┘
```

## Main Menu Bar

Needed first. Contains top-level dropdown menus with nested submenu support:

- File → New Scene, Open, Save, Save As, Import, Export, Quit
- Edit → Undo, Redo, Preferences
- View → (workspace-specific toggles)
- Window → open/switch workspaces
- Help → Docs, About

Implementation requires the `Dropdown` component with nested submenu support (not yet built).

## Workspace Header Bar

Each workspace owns its header — content is determined by that workspace. The Level Editor's header is the gizmo toolbar (Translate / Rotate / Scale / World-Local space) which already exists. A material editor header would have graph-tool controls, etc.

## Bottom Bar (UE5-inspired)

Persistent strip at the bottom edge:

- **Content Browser Drawer** — slides up from the bottom on click; can be pinned open permanently.
- Output Log toggle and status text.

## Level Editor Workspace (current)

Panels inside the docking layout:

- **Outliner** — entity hierarchy tree
- **Viewport** — main 3D render canvas
- **Properties** — selected entity component inspector (to rebuild from old ImGui panel)

## Implementation Order

1. Main menu bar + dropdown with nesting  ← next
2. Workspace tab bar (switch between workspaces)
3. Bottom bar + Content Browser drawer
4. Properties panel rebuild
5. Additional workspaces as needed


(reference image)
![[Pasted image 20260517113657.png]]