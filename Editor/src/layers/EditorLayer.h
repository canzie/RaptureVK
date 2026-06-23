#pragma once

#include "layers/Layer.h"
#include "scenes/entities/Entity.h"

#include <memory>

namespace Rapture {
class Input;
class CameraController;
class Scene;
} // namespace Rapture

/**
 * @brief Owns the editor's view: its camera, the controller driving it, and the editor input.
 */
class EditorLayer : public Rapture::Layer {
  public:
    EditorLayer();
    ~EditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(float dt) override;

  private:
    void onNewActiveScene(std::shared_ptr<Rapture::Scene> scene);

    Rapture::Entity m_cameraEntity;
    std::unique_ptr<Rapture::Input> m_input;
    std::unique_ptr<Rapture::CameraController> m_controller;
    size_t m_sceneActivatedListenerId = 0;
    bool m_registeredOnViewport = false;
};
