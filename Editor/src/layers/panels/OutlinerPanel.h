#ifndef RAPTURE__OUTLINER_PANEL_H
#define RAPTURE__OUTLINER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

#include "layers/panels/Panel.h"
#include "scenes/Scene.h"
#include <memory>

class OutlinerPanel : public Panel {
  public:
    OutlinerPanel(Amethyst::TabBar *tabBar);
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
    void buildEntityTree(Rapture::Entity entity, Amethyst::TreeRowScope &rowScope);

  private:
    Amethyst::Frame *m_root = nullptr;
    Amethyst::ScrollingFrame *m_scrollingFrame = nullptr;
    Amethyst::TreeView *m_treeView = nullptr;

    std::shared_ptr<Rapture::Scene> m_scene;
    bool m_hasScene = false;
};

#endif // RAPTURE__OUTLINER_PANEL_H
