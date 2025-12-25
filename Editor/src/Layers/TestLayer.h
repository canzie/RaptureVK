#pragma once

#include "Layers/Layer.h"
#include "Meshes/Mesh.h"
#include "Scenes/Entities/Entity.h"
#include "Shaders/Shader.h"
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Forward declarations
class ViewportPanel;

namespace Rapture {
class DBVH;
} // namespace Rapture

class TestLayer : public Rapture::Layer {
  public:
    // Define a callback for entity selection changes
    using EntitySelectedCallback = std::function<void(std::shared_ptr<Rapture::Entity>)>;

    TestLayer() : Layer("Test Layer"), m_sceneActivatedListenerId(0) {}

    virtual ~TestLayer() override;

    void onAttach() override;
    void onDetach() override;

    void onUpdate(float ts) override;

    void onNewActiveScene(std::shared_ptr<Rapture::Scene> scene);

    // Call to notify about camera changes
    void notifyCameraChange();

  private:
    // Camera references
    Rapture::Entity m_cameraEntity;
    size_t m_sceneActivatedListenerId;

    // FPS counter variables
    int m_fpsCounter = 0;
    float m_fpsTimer = 0.0f;
};
