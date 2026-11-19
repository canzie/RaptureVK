#ifndef RAPTURE__BOTTOM_BAR_H
#define RAPTURE__BOTTOM_BAR_H

#include "context/EditorContext.h"
#include "workspaces/Workspace.h"
#include <amethyst/Amethyst.h>

#include <array>
#include <cstdint>
#include <memory>

class CommandLine;
class ContentBrowserPanel;

class BottomBar {
  public:
    enum BottomBarStat {
        BOTTOM_BAR_STAT_FRAME_TIME,
        BOTTOM_BAR_STAT_FPS,
        BOTTOM_BAR_STAT_VRAM,
        BOTTOM_BAR_STAT_COUNT
    };

  public:
    BottomBar(Amethyst::Window *window, const EditorContext &context);
    ~BottomBar();
    BottomBar(const BottomBar &) = delete;
    BottomBar &operator=(const BottomBar &) = delete;
    BottomBar(BottomBar &&) = delete;
    BottomBar &operator=(BottomBar &&) = delete;

    void setCurrentWorkspace(Workspace *workspace);

    /**
     * @brief Shows the latest frame time, fps and VRAM readings
     */
    void onUpdate(void);

  private:
    void addIslandSeparator(const Amethyst::UDim2 &position);
    void setupStatsIsland(void);
    void setupContentBrowserToggle(void);
    void toggleContentBrowser(void);

    /**
     * @brief Moves the floating content browser into the current workspace's layout
     */
    void dockContentBrowser(void);

  private:
    EditorContext m_editor;
    Workspace *m_currWorkspace = nullptr;
    Amethyst::Window *m_window = nullptr;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextButton *m_contentBrowserBtn = nullptr;
    Amethyst::TextInput *m_commandInput = nullptr;
    std::unique_ptr<CommandLine> m_commandLine;
    Amethyst::Popup *m_contentBrowserPopup = nullptr;
    std::unique_ptr<ContentBrowserPanel> m_contentBrowserPanel;
    std::array<Amethyst::TextLabel *, BOTTOM_BAR_STAT_COUNT> m_statValueLabels{};
    float m_shownFrameTimeMs = -1.0f;
    uint64_t m_shownVramUsedBytes = 0;
};

#endif // RAPTURE__BOTTOM_BAR_H
