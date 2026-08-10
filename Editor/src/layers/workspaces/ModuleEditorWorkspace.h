#ifndef RAPTURE__MODULE_EDITOR_WORKSPACE_H
#define RAPTURE__MODULE_EDITOR_WORKSPACE_H

#include "Workspace.h"

#include <asset_manager/AssetCommon.h>
#include <asset_manager/AssetHandle.h>
#include <memory>

namespace Rapture {
class Instance;
class ModuleClass;
class Scene;
class Viewport;
} // namespace Rapture

class ModulePropertiesPanel;

/**
 * @brief The workspace one open module asset is edited in.
 *
 * Every module gets its properties and a save. A module authored out of scene objects also gets a
 * scene, a viewport over it and an outliner, and all that differs per class is what goes into that
 * scene and what is taken back out of it.
 */
class ModuleEditorWorkspace : public Workspace {
  public:
    /**
     * @brief Opens a module asset in the workspace its class is edited in
     * @param tabBar The workspace bar the tab is appended to
     * @param services Services the panels are built against
     * @param handle The module asset to edit
     * @return The workspace, or nullptr if the asset holds no module
     */
    static std::unique_ptr<ModuleEditorWorkspace> create(Amethyst::TabBar &tabBar, const PanelServices &services,
                                                         Rapture::AssetHandle handle);

    ~ModuleEditorWorkspace() override;

    void onUpdate(float dt) override;
    void saveLayout() override;

  protected:
    ModuleEditorWorkspace(const PanelServices &services, Rapture::AssetHandle handle);

    /**
     * @brief Builds the tab, the scene this module needs and the panels over it
     * @param tabBar The workspace bar the tab is appended to
     */
    void build(Amethyst::TabBar &tabBar);

    /**
     * @brief Whether this module is authored out of scene objects, so whether it gets a scene
     */
    virtual bool usesScene() const { return false; }

    /**
     * @brief Puts this module's scene objects into the workspace scene
     * @param parent The scene object they are parented to
     * @return The root of what was put in, or nullptr if the module has none
     */
    virtual Rapture::Instance *spawn(Rapture::Instance &parent);

    /**
     * @brief Takes the authored scene objects back into this module
     * @param root The scene object whose subtree the module takes
     */
    virtual void capture(const Rapture::Instance &root);

  protected:
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
    Rapture::AssetRef m_moduleRef;
    Rapture::ModuleClass *m_module = nullptr;

  private:
    void setupHotbar(void);
    void setupScene(void);

    /**
     * @brief Lights the preview scene, kept outside the module's own root so a save cannot take it
     */
    void setupLighting(void);
    // TODO: limit the tree panel's add menu to what the module's class can hold
    void setupPanels(Amethyst::TabBar *viewportTabBar, Amethyst::TabBar *treeTabBar, Amethyst::TabBar *propertiesTabBar);

    /**
     * @brief Takes the authored scene objects back into the module and writes the asset
     */
    void save(void);

  private:
    ModulePropertiesPanel *m_properties = nullptr;

    std::unique_ptr<Rapture::Scene> m_scene;
    Rapture::Viewport *m_viewport = nullptr;
    Rapture::Instance *m_sceneRoot = nullptr;
};

/**
 * @brief The workspace a puppet's scene objects are authored in.
 */
class PuppetEditorWorkspace : public ModuleEditorWorkspace {
  public:
    PuppetEditorWorkspace(Amethyst::TabBar &tabBar, const PanelServices &services, Rapture::AssetHandle handle);

  protected:
    bool usesScene() const override { return true; }
    Rapture::Instance *spawn(Rapture::Instance &parent) override;
    void capture(const Rapture::Instance &root) override;
};

#endif // RAPTURE__MODULE_EDITOR_WORKSPACE_H
