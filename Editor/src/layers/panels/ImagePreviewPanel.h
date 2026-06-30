#ifndef RAPTURE__IMAGE_PREVIEW_PANEL_H
#define RAPTURE__IMAGE_PREVIEW_PANEL_H

#include "layers/panels/Panel.h"

#include <amethyst/Amethyst.h>

#include <string>
#include <string_view>

/**
 * @brief Reusable panel that displays a single image filling the panel, aspect-preserved.
 */
class ImagePreviewPanel : public Panel {
  public:
    ImagePreviewPanel(Amethyst::TabBar *tabBar, const PanelServices &services, std::string_view title = "Preview");
    ~ImagePreviewPanel();

    void setImage(Amethyst::AmTextureId image);
    void clearImage();

  private:
    Amethyst::Frame *m_root = nullptr;
    Amethyst::ImageLabel *m_image = nullptr;
};

#endif // RAPTURE__IMAGE_PREVIEW_PANEL_H
