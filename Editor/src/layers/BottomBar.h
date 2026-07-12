#ifndef RAPTURE__BOTTOM_BAR_H
#define RAPTURE__BOTTOM_BAR_H

#include "layers/panels/common.h"
#include <amethyst/Amethyst.h>

#include <memory>

class ContentBrowserPanel;

class BottomBar {
  public:
    BottomBar(Amethyst::Window *window, const PanelServices &services);
    ~BottomBar();
    BottomBar(const BottomBar &) = delete;
    BottomBar &operator=(const BottomBar &) = delete;
    BottomBar(BottomBar &&) = delete;
    BottomBar &operator=(BottomBar &&) = delete;

  private:
    void setupContentBrowserToggle(void);
    void toggleContentBrowser(void);

  private:
    PanelServices m_services;
    Amethyst::Window *m_window = nullptr;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextButton *m_contentBrowserBtn = nullptr;
    Amethyst::Popup *m_contentBrowserPopup = nullptr;
    std::unique_ptr<ContentBrowserPanel> m_contentBrowserPanel;
};

#endif // RAPTURE__BOTTOM_BAR_H
