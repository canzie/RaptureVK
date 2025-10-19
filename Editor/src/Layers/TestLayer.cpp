#include "TestLayer.h"


#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "Scenes/SceneManager.h"
#include "Utils/Timestep.h"
#include "Components/Components.h"
#include "Renderer/ForwardRenderer/ForwardRenderer.h"
#include "Renderer/DeferredShading/DeferredRenderer.h"
#include "Renderer/GI/RadianceCascades/RadianceCascades.h"
#include "Materials/MaterialInstance.h"

#include "Loaders/glTF2.0/glTFLoader.h"

#include <filesystem>
#include <imgui.h>

#include "Generators/Textures/PerlinNoiseGenerator.h"

#include "Logging/TracyProfiler.h"

#include "AccelerationStructures/CPU/BVH/BVH.h"
#include "AccelerationStructures/CPU/BVH/BVH_SAH.h"
#include "AccelerationStructures/CPU/BVH/DBVH.h"
#include "Utils/Timestep.h"
#include "Meshes/MeshPrimitives.h"
#include "Physics/EntropyComponents.h"



TestLayer::~TestLayer()
{
    onDetach();
}

void TestLayer::onAttach() {

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
    //auto loader = Rapture::ModelLoadersCache::getLoader(rootPath / "assets/models/glTF2.0/Sponza/Sponza.gltf", activeScene);
    //loader->loadModel((rootPath / "assets/models/glTF2.0/Sponza/Sponza.gltf").string());

    // Load a model for testing
    //auto loader = Rapture::ModelLoadersCache::getLoader(rootPath / "assets/models/glTF2.0/MetalRoughSpheres/MetalRoughSpheres.gltf", activeScene);
    //loader->loadModel((rootPath / "assets/models/glTF2.0/MetalRoughSpheres/MetalRoughSpheres.gltf").string());

    

    auto floor = activeScene->createCube("Floor");
    floor.addComponent<Rapture::BLASComponent>(floor.getComponent<Rapture::MeshComponent>().mesh);
    floor.getComponent<Rapture::MeshComponent>().isStatic = true;
    floor.getComponent<Rapture::TransformComponent>().transforms.setScale(glm::vec3(20.0f, 2.0f, 20.0f));
    floor.getComponent<Rapture::TransformComponent>().transforms.setTranslation(glm::vec3(15.0f, 0.0f, 15.0f));


    auto wallA = activeScene->createCube("Wall A");
    wallA.addComponent<Rapture::BLASComponent>(wallA.getComponent<Rapture::MeshComponent>().mesh);
    wallA.getComponent<Rapture::MeshComponent>().isStatic = true;
    wallA.getComponent<Rapture::TransformComponent>().transforms.setScale(glm::vec3(14.0f, 12.0f, 1.5f));

    auto wallB = activeScene->createCube("Wall B");
    wallB.addComponent<Rapture::BLASComponent>(wallB.getComponent<Rapture::MeshComponent>().mesh);
    wallB.getComponent<Rapture::MeshComponent>().isStatic = true;
    wallB.getComponent<Rapture::TransformComponent>().transforms.setScale(glm::vec3(14.0f, 12.0f, 1.5f));

    auto wallC = activeScene->createCube("Wall C");
    wallC.addComponent<Rapture::BLASComponent>(wallC.getComponent<Rapture::MeshComponent>().mesh);
    wallC.getComponent<Rapture::MeshComponent>().isStatic = true;
    wallC.getComponent<Rapture::TransformComponent>().transforms.setScale(glm::vec3(14.0f, 12.0f, 1.5f));



    auto sphereA = activeScene->createSphere("Sphere A");
    sphereA.addComponent<Rapture::BLASComponent>(sphereA.getComponent<Rapture::MeshComponent>().mesh);
    sphereA.getComponent<Rapture::MeshComponent>().isStatic = true;

    auto sphereB = activeScene->createSphere("Sphere B");
    sphereB.addComponent<Rapture::BLASComponent>(sphereB.getComponent<Rapture::MeshComponent>().mesh);
    sphereB.getComponent<Rapture::MeshComponent>().isStatic = true;

    auto sphereC = activeScene->createSphere("Sphere C");
    sphereC.addComponent<Rapture::BLASComponent>(sphereC.getComponent<Rapture::MeshComponent>().mesh);
    sphereC.getComponent<Rapture::MeshComponent>().isStatic = true;

    auto sphereD = activeScene->createSphere("Sphere D");
    sphereD.addComponent<Rapture::BLASComponent>(sphereD.getComponent<Rapture::MeshComponent>().mesh);
    sphereD.getComponent<Rapture::MeshComponent>().isStatic = true;

    

    activeScene->registerBLAS(floor);
    
    activeScene->registerBLAS(wallA);
    activeScene->registerBLAS(wallB);
    activeScene->registerBLAS(wallC);

    activeScene->registerBLAS(sphereA);
    activeScene->registerBLAS(sphereB);
    activeScene->registerBLAS(sphereC);
    activeScene->registerBLAS(sphereD);




    Rapture::Entity light1 = activeScene->createSphere("Spot Light 1");
    light1.setComponent<Rapture::TransformComponent>(
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
    Rapture::Entity sunLight = activeScene->createSphere("Sun");
    sunLight.setComponent<Rapture::TransformComponent>(
        glm::vec3(-2.0f, 0.5f, -3.0f), // Position to the left of the sphere, same Z coordinate
        glm::vec3(-1.504f, 0.0f, 0.0f),               // No rotation needed for point light
        glm::vec3(0.2f)                // Small scale to make the cube compact
    );

    sunLight.addComponent<Rapture::LightComponent>(
        glm::vec3(1.0f, 1.0f, 1.0f),  // Pure white color
        1.2f                         // High intensity
    );
    sunLight.addComponent<Rapture::CascadedShadowComponent>(2048, 2048, 4, 0.8f);

    //auto perlinNoiseTexture = Rapture::PerlinNoiseGenerator::generateNoise(1024, 1024, 4, 0.5f, 2.0f, 8.0f);


    auto skybox = activeScene->createEntity("Skybox");

    skybox.addComponent<Rapture::SkyboxComponent>(rootPath / "assets/textures/cubemaps/default.cubemap");
    activeScene->setSkybox(skybox);

    try {
        activeScene->buildTLAS();
    } catch (const std::runtime_error& e) {
        Rapture::RP_ERROR("Testlayer: Failed to build TLAS: {}", e.what());
    }


    m_entropyPhysics = std::make_shared<Rapture::Entropy::EntropyPhysics>();
    m_entropyPhysics->addGlobalForceGenerator(std::make_shared<Rapture::Entropy::GravityForce>());
    m_entropyPhysics->addGlobalForceGenerator(std::make_shared<Rapture::Entropy::DampingForce>(0.4f, 0.4f));

    auto& volume =Rapture::DeferredRenderer::getDynamicDiffuseGI()->getProbeVolume();

    std::vector<Rapture::InstanceData> instanceData(volume.gridDimensions.x * volume.gridDimensions.y * volume.gridDimensions.z);

    glm::vec3 gridDimensionsF = glm::vec3(volume.gridDimensions);
    glm::vec3 gridHalfSize = (gridDimensionsF - glm::vec3(1.0f)) * volume.spacing * 0.5f;
    glm::vec3 gridStart = volume.origin - gridHalfSize;

    for (uint32_t i = 0; i < volume.gridDimensions.x; i++) {
        for (uint32_t j = 0; j < volume.gridDimensions.y; j++) {
            for (uint32_t k = 0; k < volume.gridDimensions.z; k++) {
                glm::vec3 probePosition = gridStart + glm::vec3(i, j, k) * volume.spacing;

                glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), probePosition);
                modelMatrix = glm::scale(modelMatrix, glm::vec3(0.1f)); // Scale down the probes to be visible

                size_t index = k + j * volume.gridDimensions.z + i * volume.gridDimensions.y * volume.gridDimensions.z;

                Rapture::InstanceData data;
                data.transform = modelMatrix;
                instanceData[index] = data;
            }
        }
    }

    auto sphere = std::make_shared<Rapture::Mesh>(Rapture::Primitives::CreateSphere(1, 8));
    auto ddgiVizEntity = std::make_shared<Rapture::Entity>(scene->createEntity("DDGI Probe Volume"));
    ddgiVizEntity->addComponent<Rapture::TransformComponent>();
    //ddgiVizEntity->addComponent<Rapture::InstanceShapeComponent>(instanceData, app.getVulkanContext().getVmaAllocator());
    ddgiVizEntity->addComponent<Rapture::MeshComponent>(sphere);
        

    // Create radiance cascade probe visualization
    //createRadianceCascadeProbeVisualization(activeScene);
    

}



void TestLayer::createRadianceCascadeProbeVisualization(std::shared_ptr<Rapture::Scene> scene) {
    auto& app = Rapture::Application::getInstance();
    auto radianceCascades = Rapture::DeferredRenderer::getRadianceCascades();
    
    if (!radianceCascades) {
        Rapture::RP_WARN("TestLayer: RadianceCascades not available for visualization");
        return;
    }

    const auto& cascades = radianceCascades->getCascades();
    
    // Create different colored spheres for each cascade level
    // Define colors for each cascade (rainbow-like progression)
    std::vector<glm::vec3> cascadeColors = {
        glm::vec3(1.0f, 0.0f, 0.0f),  // Red - Cascade 0 (finest detail)
        glm::vec3(1.0f, 0.5f, 0.0f),  // Orange - Cascade 1
        glm::vec3(1.0f, 1.0f, 0.0f),  // Yellow - Cascade 2
        glm::vec3(0.0f, 1.0f, 0.0f),  // Green - Cascade 3
        glm::vec3(0.0f, 0.0f, 1.0f),  // Blue - Cascade 4
        glm::vec3(0.5f, 0.0f, 1.0f)   // Purple - Cascade 5
    };
    


    for (uint32_t cascadeIdx = 0; cascadeIdx < MAX_CASCADES; cascadeIdx++) {
        const auto& cascade = cascades[cascadeIdx];
        bool enableRayVisualization = false;

    
        // Skip invalid cascades
        if (cascade.cascadeLevel == UINT32_MAX) {
            continue;
        }
        
        // Get probe positions for this cascade
        auto probePositions = radianceCascades->getCascadeProbePositions(cascadeIdx);
        
        if (probePositions.empty()) {
            continue;
        }
        
        // Create instance data for this cascade
        std::vector<Rapture::InstanceData> instanceData(probePositions.size());
        
        // Calculate scale based on cascade level (larger cascades = larger probes)
        float baseScale = 0.05f;  // Base scale for finest cascade
        float scaleMultiplier = std::pow(1.5f, static_cast<float>(cascadeIdx));  // Scale increases with cascade level
        float probeScale = baseScale * scaleMultiplier;
        
        uint32_t angularResolution = cascade.angularResolution;
        uint32_t numRays = angularResolution * angularResolution / 8;
        float minProbeDistance = cascade.minProbeDistance;
        float maxProbeDistance = cascade.maxProbeDistance;
        float rayLength = 4.0f;
        std::vector<Rapture::InstanceData> rayInstanceData;

        for (size_t i = 0; i < probePositions.size(); ++i) {
            const glm::vec3& probePos = probePositions[i];
            // Sphere instanceData as before
            glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), probePos);
            modelMatrix = glm::scale(modelMatrix, glm::vec3(probeScale));
            instanceData[i].transform = modelMatrix;

            // Ray visualization
            if (enableRayVisualization) {
            for (uint32_t rayIdx = 0; rayIdx < numRays; ++rayIdx) {
                // SphericalFibonacci direction (matches GLSL)
                const float b = (std::sqrt(5.0f) * 0.5f + 0.5f) - 1.0f;
                float phi = 2.0f * glm::pi<float>() * std::fmod(rayIdx * b, 1.0f);
                float cosTheta = 1.0f - (2.0f * float(rayIdx) + 1.0f) * (1.0f / float(numRays));
                float sinTheta = std::sqrt(std::clamp(1.0f - (cosTheta * cosTheta), 0.0f, 1.0f));
                glm::vec3 direction = glm::vec3(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
                direction = glm::normalize(direction);

                
                // Compute the center of the cube so that it starts at minProbeDistance and extends rayLength in the direction
                glm::vec3 center = probePos + direction * (minProbeDistance + rayLength * 0.5f);
                glm::vec3 xAxis(1.0f, 0.0f, 0.0f);
                float dot = glm::clamp(glm::dot(xAxis, direction), -1.0f, 1.0f);
                glm::quat rotation;
                if (dot > 0.9999f) {
                    rotation = glm::quat(1, 0, 0, 0); // No rotation
                } else if (dot < -0.9999f) {
                    rotation = glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0)); // 180 deg around Y
                } else {
                    glm::vec3 axis = glm::normalize(glm::cross(xAxis, direction));
                    float angle = std::acos(dot);
                    rotation = glm::angleAxis(angle, axis);
                }
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), center)
                                    * glm::mat4_cast(rotation)
                                    * glm::scale(glm::mat4(1.0f), glm::vec3(rayLength, 0.001f, 0.001f));
                rayInstanceData.push_back({transform});
                }
            }
            // Create entity for all rays in this cascade

        }
        
        // Create entity for this cascade
        std::string entityName = "RC Cascade " + std::to_string(cascadeIdx) + " Probes";
        auto cascadeEntity = std::make_shared<Rapture::Entity>(scene->createSphere(entityName));
        
        auto& isc = cascadeEntity->addComponent<Rapture::InstanceShapeComponent>(instanceData, app.getVulkanContext().getVmaAllocator());
        
        
        // Create material with cascade-specific color
        auto& materialC = cascadeEntity->getComponent<Rapture::MaterialComponent>();

        glm::vec3 cascadeColor = cascadeColors[cascadeIdx % cascadeColors.size()];
        materialC.material->setParameter<glm::vec3>(Rapture::ParameterID::ALBEDO, cascadeColor);
        isc.color = glm::vec4(cascadeColor, 1.0);
        

        if (enableRayVisualization) {
            auto rayEntity = std::make_shared<Rapture::Entity>(scene->createCube("RC Cascade " + std::to_string(cascadeIdx) + " Rays"));
            auto& irm = rayEntity->addComponent<Rapture::InstanceShapeComponent>(rayInstanceData, app.getVulkanContext().getVmaAllocator());
            irm.color = glm::vec4(cascadeColor, 1.0f);
            irm.useWireMode = true;
        }
    }
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


    // Get time with decimal precision
    // Use time since launch instead of time since epoch
    long long timeRawMs = Rapture::Timestep::getTimeSinceLaunchMs().count(); 
    // Convert milliseconds since launch to seconds for the shader
    float time = static_cast<float>(timeRawMs) / 1000.0f;

	// Update the camera controller
	//CameraController::update(ts);
    
    // Notify about camera changes (for ImGuizmo)
    notifyCameraChange();

    // Step simple physics (gravity) before collision detection for demo.
    if (m_entropyPhysics) {
        auto manifolds = m_entropyPhysics->step(activeScene, ts/10.0f); // need to slow it down, otherwise it is too fast

    }

    m_entropyPhysics->getCollisions()->debugVisualize(activeScene);


}




