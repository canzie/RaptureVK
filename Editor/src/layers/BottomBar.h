#ifndef RAPTURE__BOTTOM_BAR_H
#define RAPTURE__BOTTOM_BAR_H

#include "context/EditorContext.h"
#include "workspaces/Workspace.h"
#include <amethyst/Amethyst.h>

#include <memory>

class ContentBrowserPanel;

class BottomBar {
  public:
    BottomBar(Amethyst::Window *window, const EditorContext &context);
    ~BottomBar();
    BottomBar(const BottomBar &) = delete;
    BottomBar &operator=(const BottomBar &) = delete;
    BottomBar(BottomBar &&) = delete;
    BottomBar &operator=(BottomBar &&) = delete;

    void setCurrentWorkspace(Workspace *workspace);

  private:
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
    Amethyst::Popup *m_contentBrowserPopup = nullptr;
    std::unique_ptr<ContentBrowserPanel> m_contentBrowserPanel;
};

#endif // RAPTURE__BOTTOM_BAR_H
