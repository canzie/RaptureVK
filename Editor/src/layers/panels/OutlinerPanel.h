#ifndef RAPTURE__OUTLINER_PANEL_H
#define RAPTURE__OUTLINER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/common.h>
#include <components/docking_layer.h>
#include <components/frame.h>
#include <components/panel_layer.h>
#include <components/scrolling_frame.h>
#include <components/text_button.h>
#include <components/text_label.h>
#include <components/tree_view.h>

#include "scenes/Scene.h"
#include <memory>

class OutlinerPanel {
  public:
    OutlinerPanel(Amethyst::DockingLayer *dockingLayer);
    ~OutlinerPanel();
    OutlinerPanel(const OutlinerPanel &) = delete;
    OutlinerPanel &operator=(const OutlinerPanel &) = delete;
    OutlinerPanel(OutlinerPanel &&) = delete;
    OutlinerPanel &operator=(OutlinerPanel &&) = delete;

    /**
     * @brief Update the outliner with the current scene
     */
    void setScene(std::shared_ptr<Rapture::Scene> scene);

    /**
     * @brief Refresh the tree view with current scene hierarchy
     */
    void refresh();

  private:
    void buildEntityTree(Rapture::Entity entity, uint32_t parentRow);

  private:
    Amethyst::DockingLayer *m_dockingLayer = nullptr;
    Amethyst::PanelLayer *m_root = nullptr;
    Amethyst::Frame *m_background = nullptr;
    Amethyst::ScrollingFrame *m_scrollingFrame = nullptr;
    Amethyst::TreeView *m_treeView = nullptr;

    std::shared_ptr<Rapture::Scene> m_scene;
    bool m_hasScene = false;
};

#endif // RAPTURE__OUTLINER_PANEL_H
