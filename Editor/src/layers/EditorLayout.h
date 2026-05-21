#ifndef RAPTURE__EDITOR_LAYOUT_H
#define RAPTURE__EDITOR_LAYOUT_H

constexpr float EDITOR_MENU_BAR_HEIGHT     = 28.0f;
constexpr float EDITOR_WORKSPACE_TAB_HEIGHT = 32.0f;
constexpr float EDITOR_HOTBAR_HEIGHT       = 40.0f;
constexpr float EDITOR_BOTTOM_BAR_HEIGHT   = 28.0f;

// TabBar total height = tab strip + hotbar content area
constexpr float EDITOR_TABBAR_TOTAL_HEIGHT = EDITOR_WORKSPACE_TAB_HEIGHT + EDITOR_HOTBAR_HEIGHT;

// Y where per-workspace DockingLayers begin
constexpr float EDITOR_CONTENT_TOP = EDITOR_MENU_BAR_HEIGHT + EDITOR_TABBAR_TOTAL_HEIGHT;

constexpr float EDITOR_DOCK_SPACING = 4.0f;

#endif // RAPTURE__EDITOR_LAYOUT_H
