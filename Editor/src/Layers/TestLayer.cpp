#include "TestLayer.h"


#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "Scenes/SceneManager.h"
#include "Utils/Timestep.h"
#include "Components/Components.h"
#include "Renderer/ForwardRenderer/ForwardRenderer.h"
#include "Renderer/DeferredShading/DeferredRenderer.h"

#include "Loaders/glTF2.0/glTFLoader.h"

#include <filesystem>
#include <imgui.h>

#include "Generators/Textures/PerlinNoiseGenerator.h"

#include "Logging/TracyProfiler.h"

TestLayer::~TestLayer()
{
    onDetach();
}

void TestLayer::onAttach()
{

    Rapture::RP_INFO("TestLayer attached");

        // Register for scene activation events - store the ID for cleanup
    m_sceneActivatedListenerId = Rapture::GameEvents::onSceneActivated().addListener(
        [this](std::shared_ptr<Rapture::Scene> scene) {
            Rapture::RP_INFO("TestLayer::onSceneActivated - New active scene: {0}", scene->getSceneName());
            onNewActiveScene(scene);
        });

    // Check if a scene is already active when the layer is attached
    // This handles the case where the initial scene is set before this layer's listener is registered
    auto currentActiveScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (currentActiveScene) {
        Rapture::RP_INFO("TestLayer::onAttach - Initial scene already active: {0}", currentActiveScene->getSceneName());
        onNewActiveScene(currentActiveScene);
    }

    // Initialize FPS counter variables
    m_fpsCounter = 0;
    m_fpsTimer = 0.0f;
}

void TestLayer::onNewActiveScene(std::shared_ptr<Rapture::Scene> scene)
{
    auto activeScene = scene;

    if (!activeScene) {
        Rapture::RP_ERROR("No active scene found");
        return;
    }

    // Create camera entity
    m_cameraEntity = std::make_shared<Rapture::Entity>(activeScene->createEntity("Main Camera"));
    activeScene->setMainCamera(m_cameraEntity);

    // Add transform component (position camera back a bit)
    auto& transform = m_cameraEntity->addComponent<Rapture::TransformComponent>(
        glm::vec3(0.0f, 0.0f, 3.0f),  // Position
        glm::vec3(0.0f, 0.0f, 0.0f),  // Rotation 
        glm::vec3(1.0f, 1.0f, 1.0f)   // Scale
    );
    
    // Add camera component
    auto& camera = m_cameraEntity->addComponent<Rapture::CameraComponent>(90.0f, 16.0f/9.0f, 0.1f, 100.0f);
    camera.isMainCamera = true;

    
    // Add camera controller component and set up input
    auto& controller = m_cameraEntity->addComponent<Rapture::CameraControllerComponent>();
    controller.controller.mouseSensitivity = 0.1f;
    controller.controller.movementSpeed = 5.0f;

    auto& app = Rapture::Application::getInstance();
    auto& project = app.getProject();


    auto rootPath = project.getProjectRootDirectory();

    // Load a model for testing
    auto loader = Rapture::ModelLoadersCache::getLoader(rootPath / "assets/models/glTF2.0/Sponza/Sponza.gltf", activeScene);
    loader->loadModel((rootPath / "assets/models/glTF2.0/Sponza/Sponza.gltf").string());

    // Load a model for testing
    //auto loader = Rapture::ModelLoadersCache::getLoader(rootPath / "assets/models/glTF2.0/MetalRoughSpheres/MetalRoughSpheres.gltf", activeScene);
    //loader->loadModel((rootPath / "assets/models/glTF2.0/MetalRoughSpheres/MetalRoughSpheres.gltf").string());


    Rapture::Entity light1 = activeScene->createEntity("Spot Light 1");
    light1.addComponent<Rapture::TransformComponent>(
        glm::vec3(2.0f, 1.0f, -3.0f),  // Position to the right of the sphere, same Z coordinate
        glm::vec3(-2.243f, 0.0f, 0.0f),              // No rotation needed for point light
        glm::vec3(0.2f)               // Small scale to make the cube compact
    );


    light1.addComponent<Rapture::LightComponent>(
        glm::vec3(1.0f, 1.0f, 1.0f),  // Pure white color
        1.2f,                         // High intensity
        10.0f,                         // Range
        30.0f,                         // Inner cone angle
        45.0f                          // Outer cone angle
    );

    light1.addComponent<Rapture::ShadowComponent>(2048, 2048);

    // Light 2: A blue-tinted light to the left side
    Rapture::Entity sunLight = activeScene->createEntity("Sun");
    sunLight.addComponent<Rapture::TransformComponent>(
        glm::vec3(-2.0f, 0.5f, -3.0f), // Position to the left of the sphere, same Z coordinate
        glm::vec3(-1.504f, 0.0f, 0.0f),               // No rotation needed for point light
        glm::vec3(0.2f)                // Small scale to make the cube compact
    );

    sunLight.addComponent<Rapture::LightComponent>(
        glm::vec3(1.0f, 1.0f, 1.0f),  // Pure white color
        1.2f                         // High intensity
    );
    sunLight.addComponent<Rapture::CascadedShadowComponent>(2048, 2048, 4, 0.8f);
    auto& sm = sunLight.addComponent<Rapture::ShadowComponent>(4096, 4096);
    sm.isActive = false;

    auto perlinNoiseTexture = Rapture::PerlinNoiseGenerator::generateNoise(1024, 1024, 4, 0.5f, 2.0f, 8.0f);


    auto skybox = activeScene->createEntity("Skybox");

    skybox.addComponent<Rapture::SkyboxComponent>(rootPath / "assets/textures/cubemaps/default.cubemap");
    activeScene->setSkybox(skybox);

    Rapture::RP_INFO("Created camera entity in scene: {0}", activeScene->getSceneName());
}




void TestLayer::onDetach()
{
    if (m_sceneActivatedListenerId != 0) {
        Rapture::GameEvents::onSceneActivated().removeListener(m_sceneActivatedListenerId);
        m_sceneActivatedListenerId = 0;
    }
}

void TestLayer::notifyCameraChange()
{


}

void TestLayer::onUpdate(float ts)
{

    RAPTURE_PROFILE_SCOPE("TestLayer::onUpdate");
    // Get the active scene from SceneManager
    auto activeScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (!activeScene) return;
    
    // Update camera if it exists
    if (m_cameraEntity && m_cameraEntity->hasComponent<Rapture::CameraControllerComponent>()) {
        auto& controller = m_cameraEntity->getComponent<Rapture::CameraControllerComponent>();
        auto& transform = m_cameraEntity->getComponent<Rapture::TransformComponent>();
        auto& camera = m_cameraEntity->getComponent<Rapture::CameraComponent>();
        
        // Update camera using the simplified controller method
        controller.update(ts, transform, camera);
    }
    
    // Update FPS counter
    m_fpsCounter++;
    m_fpsTimer += ts;
    
    // Log FPS approximately once per second
    if (m_fpsTimer >= 1.0f) {
        float fps = static_cast<float>(m_fpsCounter) / m_fpsTimer;
        Rapture::RP_INFO("FPS: {0:.1f}", fps);
        
        // Reset counters
        m_fpsCounter = 0;
        m_fpsTimer = 0.0f;
    }

    auto& registry = activeScene->getRegistry();

    // Get time with decimal precision
    // Use time since launch instead of time since epoch
    long long timeRawMs = Rapture::Timestep::getTimeSinceLaunchMs().count(); 
    // Convert milliseconds since launch to seconds for the shader
    float time = static_cast<float>(timeRawMs) / 1000.0f;

	// Update the camera controller
	//CameraController::update(ts);
    
    // Notify about camera changes (for ImGuizmo)
    notifyCameraChange();


    //Rapture::ForwardRenderer::drawFrame(activeScene);
    Rapture::DeferredRenderer::drawFrame(activeScene);

}




