#ifndef RAPTURE__PREVIEW_WORKSPACE_H
#define RAPTURE__PREVIEW_WORKSPACE_H

#include "AssetWorkspace.h"
#include "scene/PreviewScene.h"

/**
 * @brief A workspace showing its asset in a scene of its own
 */
class PreviewWorkspace : public AssetWorkspace {
  public:
    ~PreviewWorkspace() override;

    void onUpdate(float dt) override;

  protected:
    /**
     * @brief Creates the workspace and the scene it shows its asset in
     * @param context What the whole editor reaches
     * @param handle The asset this workspace is for
     * @param previewSceneName Name the preview scene and its viewport go by
     */
    PreviewWorkspace(const EditorContext &context, Rapture::AssetHandle handle, std::string_view previewSceneName);

    PreviewScene &preview() { return m_preview; }

  private:
    PreviewScene m_preview;
};

#endif // RAPTURE__PREVIEW_WORKSPACE_H
