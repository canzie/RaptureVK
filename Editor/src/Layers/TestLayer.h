#pragma once

#include <memory>
#include <functional>
#include "Layers/Layer.h"
#include "Scenes/Scene.h"
#include "Meshes/Mesh.h"
#include "Shaders/Shader.h"
#include <vector>
#include <glm/glm.hpp>

#include "Physics/Entropy.h"

// Forward declarations
class ViewportPanel;

namespace Rapture { class DBVH; }

class TestLayer : public Rapture::Layer
{
public:
	// Define a callback for entity selection changes
	using EntitySelectedCallback = std::function<void(std::shared_ptr<Rapture::Entity>)>;

	TestLayer()
		: Layer("Test Layer"),
		m_sceneActivatedListenerId(0)
	{
	}

    virtual ~TestLayer() override;

	void onAttach() override;
	void onDetach() override;

	void onUpdate(float ts) override;

    void onNewActiveScene(std::shared_ptr<Rapture::Scene> scene);
    

    // Call to notify about camera changes
    void notifyCameraChange();

private:
    void createRadianceCascadeProbeVisualization(std::shared_ptr<Rapture::Scene> scene);

    
    // Camera references
    std::shared_ptr<Rapture::Entity> m_cameraEntity;
    size_t m_sceneActivatedListenerId;

    // FPS counter variables
    int m_fpsCounter = 0;
    float m_fpsTimer = 0.0f;

    std::shared_ptr<Rapture::Entropy::EntropyPhysics> m_entropyPhysics;
    
    std::vector<std::shared_ptr<Rapture::Entity>> m_cascadeProbeEntities;

};
