#ifndef RAPTURE__EDITOR_LAYER_H
#define RAPTURE__EDITOR_LAYER_H

#include "layers/Layer.h"
#include "scenes/entities/Entity.h"

#include <memory>
#include <unordered_map>

namespace Rapture {
class Input;
class CameraController;
class Viewport;
} // namespace Rapture

/**
 * @brief Owns the editor's view controls: a camera + controller per editor viewport, plus the editor input.
 */
class EditorLayer : public Rapture::Layer {
  public:
    EditorLayer();
    ~EditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(float dt) override;

  private:
    struct ViewportControl {
        Rapture::Entity camera;
        std::unique_ptr<Rapture::CameraController> controller;
    };

    /**
     * @brief Create controls for newly appeared viewports and drop controls for destroyed ones.
     */
    void syncViewportControls(void);

    std::unique_ptr<Rapture::Input> m_input;
    std::unordered_map<Rapture::Viewport *, ViewportControl> m_controls;
};

#endif // RAPTURE__EDITOR_LAYER_H
