
# File Browser — Layout Reference

A Blender-inspired file-open / save-as dialog. This bundle contains a self-contained HTML reference (`File Browser Dialog.html`) showing the three main states. Colors are placeholder — apply your own tokens.

---

## Structure (top → bottom)

```
┌─────────────────────────────────────────────────────────────┐
│  fb-top        44 px   nav · path bar · search              │
├────────────────┬────────────────────────────────────────────┤
│  fb-side       │  fb-main                                   │
│  220 px (var)  │  flex: 1                                   │
│                │  ┌──────────────────────────────────────┐  │
│  collapsible   │  │ fb-list-hd  28 px  sortable header   │  │
│  bookmark      │  ├──────────────────────────────────────┤  │
│  sections      │  │ fb-scroll  (overflow-y: auto)        │  │
│                │  │   fb-row × N  height: --fb-row-h      │  │
│                │  └──────────────────────────────────────┘  │
├────────────────┴────────────────────────────────────────────┤
│  fb-statusbar  24 px   item count · selection info          │
├─────────────────────────────────────────────────────────────┤
│  fb-foot       52 px   filename field · filter · actions    │
└─────────────────────────────────────────────────────────────┘
```

---

## Top bar — `fb-top`

Height: **44 px**. Flex row, 8 px gap, 10 px horizontal padding.

| Slot | Class | Width | Notes |
|---|---|---|---|
| Nav buttons | `fb-navgroup` | auto | back · forward · up · refresh — 28×26 px each |
| New folder | `fb-navbtn` (standalone) | 30×30 px | icon button with border |
| Path string | `fb-path` | flex: 1 | editable plain text input — NOT breadcrumbs. Users paste a full path string here. When focused: accent border + blinking caret. Monospace font. |
| Search | `fb-search` | 200 px fixed | right-aligned filter input |

---

## Sidebar — `fb-side`

Width: `var(--fb-side-w)`, default **220 px** (tweak-driven, range 160–300 px).  
Background slightly darker than main panel. Right border separator.

Each collapsible section:
- **Header** `fb-sec-hd`: 26 px tall, uppercase 11 px label + rotating caret (−90° when collapsed)
- **Items** `fb-sec-items`: 6 px padding, 1 px gap between items
- **Item** `fb-bm`: 26 px tall, 8 px horizontal padding, 8 px icon gap. Active item gets accent-tinted background.

Sections (in order): **Favorites · System · Project · Recent**

---

## File list — `fb-main`

### Column grid (CSS `grid-template-columns`)

```css
grid-template-columns: 30px  1fr   92px   132px  152px;
/*                     icon  name  size   type   date  */
```

Both `.fb-list-hd` and `.fb-row` use the same grid — headers and rows stay perfectly aligned. Right padding 10 px (scrollbar clearance).

### Header — `fb-list-hd`

Height: **28 px**. Columns are uppercase 11 px, letter-spacing 0.03em.  
Active sort column shows an accent chevron (↑ asc / ↓ desc).  
Size column right-aligned (`justify-content: flex-end`).

### Rows — `fb-row`

Height: `var(--fb-row-h)` — tied to density:

| Density | `--fb-row-h` |
|---|---|
| compact | 26 px |
| cozy (default) | 30 px |
| comfy | 36 px |

Row states:
- **default**: transparent (even rows get a very subtle tint via `--bg-row-alt`)
- **hover**: `--bg-hover` (white @ 5% opacity)
- **selected** `.sel`: `--bg-selected` (accent @ 24% opacity) + 1 px top/bottom accent border at 45% opacity
- **folder** `.is-folder`: icon cell gets accent color; name is slightly brighter

Cell notes:
- `.col-ico` — 30 px, centered
- `.col-name` — 12 px text, 7 px gap for any inline icon
- `.col-size` — right-aligned, monospace 11 px, muted color
- `.col-type` — 11 px, muted
- `.col-date` — monospace 11 px, muted

Folders render `—` in the size cell (no size).

---

## Status bar — `fb-statusbar`

Height: **24 px**. Flex row, 10 px gap, 12 px padding.  
Font size 11 px, tertiary color. Shows total item count on the left, selected file + size on the right.

---

## Footer — `fb-foot`

Height: **52 px**. Flex row, 10 px gap, 12 px padding.

| Slot | Width | Notes |
|---|---|---|
| Label | auto | "File" (open mode) or "Save As" (write mode) — 12 px, muted |
| Filename field `fb-filename` | flex: 1 | Shows selected filename in open mode. In write mode (`.write`): editable input with blinking caret, accent border on focus. Empty state uses tertiary color. |
| Filter dropdown `fb-filter` | auto | "All Assets" + caret. 30 px height. |
| Cancel `fb-btn` | 84 px min | Secondary button, 30 px height |
| Primary action `fb-btn.primary` | 84 px min | "Import" / "Export" / "Open". Accent background. |

---

## Key CSS variables (color-agnostic names)

Apply your own values — these are the vars the component reads:

```
--bg-app          outer chrome / gaps
--bg-toolbar      top bar + footer background
--bg-panel        sidebar + list background
--bg-panel-2      header row + status bar background
--bg-input        path bar + filename field background
--bg-hover        row hover tint
--bg-row-alt      even-row subtle tint
--bg-selected     selected row fill (accent-derived)

--line            primary border
--line-2          slightly brighter border (hover states)
--line-strong     strong border (button hover)

--fg              primary text
--fg-strong       folder names / selected row text
--fg-muted        labels, secondary text
--fg-dim          icons, tertiary text
--fg-tertiary     very dim (placeholder text, status bar)

--accent          active sort caret, selected row border, primary button bg
--accent-hi       folder icons, active sidebar item icon

--font            UI font family
--font-mono       path string, size/date cells, status bar
--fb-row-h        row height (density-driven, default 30px)
--fb-side-w       sidebar width (tweak-driven, default 220px)
--r-sm/md/lg      border radius (2/3/4 px)
```

---

## Interactions

- **Clicking a row** → selects it; filename field updates to match
- **Double-clicking a folder** → navigates into it (path bar updates)
- **Clicking a section header** → toggles collapse (caret rotates)
- **Clicking a sidebar bookmark** → navigates to that path
- **Path bar click** → focuses for editing; user can paste any absolute path
- **Sort column click** → sorts by that column; second click reverses direction
- **Back/Forward** → browser-history-style navigation
- **Up** → goes to parent directory
- **Refresh** → reloads current directory

---

## Files in this bundle

- `File Browser Dialog.html` — self-contained visual reference (3 states)
- `README.md` — this document
