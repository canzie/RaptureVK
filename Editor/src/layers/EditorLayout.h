#ifndef RAPTURE__EDITOR_LAYOUT_H
#define RAPTURE__EDITOR_LAYOUT_H

#include <cstdint>

constexpr float EDITOR_MENU_BAR_HEIGHT     = 28.0f;
constexpr float EDITOR_WORKSPACE_TAB_HEIGHT = 32.0f;
constexpr float EDITOR_HOTBAR_HEIGHT       = 40.0f;
constexpr float EDITOR_HOTBAR_PADDING = 8.0f;
constexpr float EDITOR_HOTBAR_SEPARATOR_WIDTH = 1.0f;
constexpr float EDITOR_BOTTOM_BAR_HEIGHT   = 28.0f;

constexpr int32_t LAUNCHER_WINDOW_WIDTH = 640;
constexpr int32_t LAUNCHER_WINDOW_HEIGHT = 520;

constexpr int32_t FILE_EXPLORER_WINDOW_WIDTH = 880;
constexpr int32_t FILE_EXPLORER_WINDOW_HEIGHT = 560;

// TabBar total height = tab strip + hotbar content area
constexpr float EDITOR_TABBAR_TOTAL_HEIGHT = EDITOR_WORKSPACE_TAB_HEIGHT + EDITOR_HOTBAR_HEIGHT;

// Y where per-workspace DockingLayers begin
constexpr float EDITOR_CONTENT_TOP = EDITOR_MENU_BAR_HEIGHT + EDITOR_TABBAR_TOTAL_HEIGHT;

constexpr float EDITOR_DOCK_SPACING = 4.0f;
constexpr float EDITOR_DOCK_INNER_SPACING = 3.0f;

#endif // RAPTURE__EDITOR_LAYOUT_H
