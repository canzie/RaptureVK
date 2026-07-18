#ifndef RAPTURE__IMAGE_PREVIEW_PANEL_H
#define RAPTURE__IMAGE_PREVIEW_PANEL_H

#include "layers/panels/Panel.h"

#include "asset_manager/AssetCommon.h"

#include <amethyst/Amethyst.h>

#include <unordered_map>

/**
 * @brief Source that feeds the displayed image.
 *
 * EXTERNAL: another system pushes the image via setImage (e.g. the texture generator).
 * ASSET_PICKER: the panel owns a dropdown listing runtime texture assets and displays the selected one.
 */
enum class ImagePreviewMode {
    EXTERNAL,
    ASSET_PICKER
};

/**
 * @brief Reusable panel that displays a single image filling the panel, aspect-preserved.
 */
class ImagePreviewPanel : public Panel {
  public:
    ImagePreviewPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, std::string title = "Preview",
                      ImagePreviewMode mode = ImagePreviewMode::EXTERNAL);
    ~ImagePreviewPanel();

    void setImage(Amethyst::AmTextureId image);
    void clearImage();

  private:
    void rebuildSelector();
    void selectTexture(Rapture::AssetHandle handle);

    Amethyst::ImageLabel *m_image = nullptr;
    Amethyst::Dropdown *m_selector = nullptr;

    ImagePreviewMode m_mode;
    std::unordered_map<Rapture::AssetHandle, Amethyst::AmTextureId> m_registered;
};

#endif // RAPTURE__IMAGE_PREVIEW_PANEL_H
