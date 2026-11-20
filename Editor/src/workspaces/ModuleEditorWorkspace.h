#ifndef RAPTURE__MODULE_EDITOR_WORKSPACE_H
#define RAPTURE__MODULE_EDITOR_WORKSPACE_H

#include "AssetWorkspace.h"

#include <components/context_menu.h>
#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/modules/AModule.h>
#include <rapture/renderer/viewport/Viewport.h>

#include <memory>

namespace Rapture {
class SceneObject;
class SerialDocument;
class Scene;
} // namespace Rapture

class EditorCamera;
class IconLabelButton;

/**
 * @brief The workspace one open module is authored in, its subtree spawned into a scene of its own
 */
class ModuleEditorWorkspace : public AssetWorkspace {
  public:
    std::string_view typeName() const override { return "ModuleEditorWorkspace"; }
    ModuleEditorWorkspace(const EditorContext &context, Rapture::AssetHandle handle);
    ~ModuleEditorWorkspace() override;

    void onUpdate(float dt) override;

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;

  private:
    /**
     * @brief Creates the scene, its sun, the module's subtree and the viewport looking at them
     */
    void setupScene();

    /**
     * @brief Reads the asset's document into the scene
     * @return The spawned root, or a fresh empty one if the asset has never been authored
     */
    Rapture::SceneObject *spawnModuleRoot();

    /**
     * @brief The scene object an added object is parented to, which is the selected one or the module's root
     */
    Rapture::SceneObject *getAddTarget() const;

    /**
     * @brief Opens the menu of scene objects that can be added, below the hotbar's Add button
     */
    void showAddMenu();

    /**
     * @brief Writes the authored subtree back into the asset and saves it
     */
    void save();

  private:
    Rapture::Ref<Rapture::AModule> m_documentRef;
    Rapture::SerialDocument *m_document = nullptr;

    std::unique_ptr<Rapture::Scene> m_scene;
    std::unique_ptr<EditorCamera> m_editorCamera;
    Rapture::ViewportContext m_viewport;
    Rapture::SceneObject *m_moduleRoot = nullptr;

    IconLabelButton *m_addButton = nullptr;
    Amethyst::ContextMenu *m_addMenu = nullptr;
};

#endif // RAPTURE__MODULE_EDITOR_WORKSPACE_H
