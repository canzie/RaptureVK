#ifndef RAPTURE__SCENE_OBJECT_EDITOR_WORKSPACE_H
#define RAPTURE__SCENE_OBJECT_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <asset_manager/AssetCommon.h>
#include <asset_manager/AssetHandle.h>
#include <viewport/Viewport.h>

#include <memory>

namespace Rapture {
class SceneObject;
class SerialDocument;
class Scene;
class Viewport;
} // namespace Rapture

/**
 * @brief The workspace one open scene object asset is edited in.
 *
 * The asset holds a subtree as a document, so editing it is spawning that subtree into a scene of
 * its own, authoring it through the same viewport, tree and properties panels a world uses, and
 * writing the root back out on save.
 */
class SceneObjectEditorWorkspace : public Workspace {
  public:
    /**
     * @brief Opens a scene object asset in its workspace
     * @param tabBar The workspace bar the tab is appended to
     * @param services Services the panels are built against
     * @param handle The scene object asset to edit
     * @return The workspace, or nullptr if the asset holds no document
     */
    static std::unique_ptr<SceneObjectEditorWorkspace> create(Amethyst::TabBar &tabBar, const PanelServices &services,
                                                              Rapture::AssetHandle handle);

    ~SceneObjectEditorWorkspace() override;

    void onUpdate(float dt) override;
    void saveLayout() override;

    static constexpr std::string_view staticKind() { return "sceneObjectEditor"; }

  private:
    SceneObjectEditorWorkspace(const PanelServices &services, Rapture::AssetHandle handle);

    /**
     * @brief Builds the tab, the scene the asset is authored in and the panels over it
     * @param tabBar The workspace bar the tab is appended to
     */
    void build(Amethyst::TabBar &tabBar);

    void setupHotbar(void);
    void setupScene(void);

    /**
     * @brief Lights the preview scene, kept outside the asset's own root so a save cannot take it
     */
    void setupLighting(void);
    void setupPanels(Amethyst::TabBar *viewportTabBar, Amethyst::TabBar *treeTabBar, Amethyst::TabBar *propertiesTabBar);

    /**
     * @brief Reads the asset's document into the workspace scene
     * @return The spawned root, or a fresh empty one if the asset has never been authored
     */
    Rapture::SceneObject *spawn(void);

    /**
     * @brief Writes the authored subtree back into the asset and saves it
     */
    void save(void);

  private:
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
    Rapture::AssetRef m_documentRef;
    Rapture::SerialDocument *m_document = nullptr;

    std::unique_ptr<Rapture::Scene> m_scene;
    Rapture::ViewportContext m_viewport;
    Rapture::SceneObject *m_sceneRoot = nullptr;
};

#endif // RAPTURE__SCENE_OBJECT_EDITOR_WORKSPACE_H
