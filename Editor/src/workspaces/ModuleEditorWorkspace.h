#ifndef RAPTURE__MODULE_EDITOR_WORKSPACE_H
#define RAPTURE__MODULE_EDITOR_WORKSPACE_H

#include "AssetWorkspace.h"

#include <assets/asset_manager/Asset.h>
#include <assets/modules/AModule.h>
#include <renderer/viewport/Viewport.h>

#include <memory>

namespace Rapture {
class SceneObject;
class SerialDocument;
class Scene;
} // namespace Rapture

/**
 * @brief The workspace one open module is edited in.
 *
 * The asset holds a subtree as a document, so editing it is spawning that subtree into a scene of
 * its own, authoring it through the same viewport, tree and properties panels a world uses, and
 * writing the root back out on save.
 */
class ModuleEditorWorkspace : public AssetWorkspace {
  public:
    ModuleEditorWorkspace(const EditorContext &context, Rapture::AssetHandle handle);
    ~ModuleEditorWorkspace() override;

    void onUpdate(float dt) override;

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;

  private:
    void setupScene();

    /**
     * @brief Lights the scene, kept outside the asset's own root so a save cannot take it
     */
    void setupLighting();

    /**
     * @brief Reads the asset's document into the workspace scene
     * @return The spawned root, or a fresh empty one if the asset has never been authored
     */
    Rapture::SceneObject *spawn();

    /**
     * @brief Writes the authored subtree back into the asset and saves it
     */
    void save();

  private:
    Rapture::Ref<Rapture::AModule> m_documentRef;
    Rapture::SerialDocument *m_document = nullptr;

    std::unique_ptr<Rapture::Scene> m_scene;
    Rapture::ViewportContext m_viewport;
    Rapture::SceneObject *m_sceneRoot = nullptr;
};

#endif // RAPTURE__MODULE_EDITOR_WORKSPACE_H
