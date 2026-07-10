#include "TestLayer.h"

#include "components/Components.h"
#include "generators/terrain/TerrainTypes.h"
#include "logging/Log.h"
#include "renderer/DeferredRenderer.h"
#include "scenes/Scene.h"
#include "scenes/SceneManager.h"
#include "utils/Timestep.h"
#include "window_context/Application.h"

#include "loaders/gltf/glTFLoader.h"

#include <filesystem>

#include "logging/TracyProfiler.h"

#include "acceleration_structures/cpu/bvh/BVH.h"
#include "acceleration_structures/cpu/bvh/BVH_SAH.h"
#include "acceleration_structures/cpu/bvh/DBVH.h"
#include "asset_manager/AssetManager.h"
#include "components/TerrainComponent.h"
#include "generators/textures/ProceduralTextures.h"
#include "materials/Material.h"
#include "materials/MaterialInstance.h"
#include "materials/graph/SurfaceGraphManager.h"
#include "utils/Timestep.h"

/**
 * @brief Graph 0: fract(position * scale) * tint feeding albedo
 * @return The authored graph
 */
static Rapture::MaterialGraph s_buildFractTintGraph()
{
    using GN = Rapture::GraphNodeType;
    Rapture::MaterialGraph graph;
    graph.name = "Graph0";
    graph.nodes.push_back({.id = 1, .type = GN::POSITION});
    graph.nodes.push_back({.id = 2, .type = GN::CONSTANT_VEC3, .inputValues = {Rapture::PinValue(glm::vec3(0.5f))}});
    graph.nodes.push_back({.id = 3, .type = GN::MULTIPLY_VEC3});
    graph.nodes.push_back({.id = 4, .type = GN::FRACT_VEC3});
    graph.nodes.push_back({.id = 5, .type = GN::CONSTANT_VEC3, .inputValues = {Rapture::PinValue(glm::vec3(1.0f, 0.4f, 0.2f))}});
    graph.nodes.push_back({.id = 6, .type = GN::MULTIPLY_VEC3});
    graph.nodes.push_back({.id = 7, .type = GN::SURFACE_OUTPUT});
    graph.connections.push_back({.srcNode = 1, .dstNode = 3, .dstPin = 0});
    graph.connections.push_back({.srcNode = 2, .dstNode = 3, .dstPin = 1});
    graph.connections.push_back({.srcNode = 3, .dstNode = 4, .dstPin = 0});
    graph.connections.push_back({.srcNode = 4, .dstNode = 6, .dstPin = 0});
    graph.connections.push_back({.srcNode = 5, .dstNode = 6, .dstPin = 1});
    graph.connections.push_back({.srcNode = 6, .dstNode = 7, .dstPin = 0});
    graph.outputNodeId = 7;
    return graph;
}

/**
 * @brief Graph 1: diagonal sine bands from uv, two colors mixed, roughness from luminance
 *
 * sin((u + v) * freq) remapped to 0..1 drives mix(colorA, colorB). Exercises the newer nodes:
 * SPLIT_VEC2 (multi output), REMAP_FLOAT (partly default range), MIX_VEC3 and LUMINANCE.
 * @return The authored graph
 */
static Rapture::MaterialGraph s_buildSineBandGraph()
{
    using GN = Rapture::GraphNodeType;
    Rapture::MaterialGraph graph;
    graph.name = "Graph1";
    graph.nodes.push_back({.id = 1, .type = GN::TEXCOORD});
    graph.nodes.push_back({.id = 2, .type = GN::SPLIT_VEC2});
    graph.nodes.push_back({.id = 3, .type = GN::CONSTANT_FLOAT, .inputValues = {Rapture::PinValue(20.0f)}});
    graph.nodes.push_back({.id = 4, .type = GN::MULTIPLY_FLOAT});
    graph.nodes.push_back({.id = 5, .type = GN::MULTIPLY_FLOAT});
    graph.nodes.push_back({.id = 6, .type = GN::ADD_FLOAT});
    graph.nodes.push_back({.id = 7, .type = GN::SIN_FLOAT});
    graph.nodes.push_back({.id = 8, .type = GN::CONSTANT_FLOAT, .inputValues = {Rapture::PinValue(-1.0f)}});
    graph.nodes.push_back({.id = 9, .type = GN::REMAP_FLOAT});
    graph.nodes.push_back({.id = 10, .type = GN::CONSTANT_VEC3, .inputValues = {Rapture::PinValue(glm::vec3(0.9f, 0.1f, 0.1f))}});
    graph.nodes.push_back({.id = 11, .type = GN::CONSTANT_VEC3, .inputValues = {Rapture::PinValue(glm::vec3(0.1f, 0.3f, 0.9f))}});
    graph.nodes.push_back({.id = 12, .type = GN::MIX_VEC3});
    graph.nodes.push_back({.id = 13, .type = GN::LUMINANCE});
    graph.nodes.push_back({.id = 14, .type = GN::SURFACE_OUTPUT});
    // u * freq and v * freq, then sum for the diagonal
    graph.connections.push_back({.srcNode = 1, .dstNode = 2, .dstPin = 0});
    graph.connections.push_back({.srcNode = 2, .srcPin = 0, .dstNode = 4, .dstPin = 0});
    graph.connections.push_back({.srcNode = 3, .dstNode = 4, .dstPin = 1});
    graph.connections.push_back({.srcNode = 2, .srcPin = 1, .dstNode = 5, .dstPin = 0});
    graph.connections.push_back({.srcNode = 3, .dstNode = 5, .dstPin = 1});
    graph.connections.push_back({.srcNode = 4, .dstNode = 6, .dstPin = 0});
    graph.connections.push_back({.srcNode = 5, .dstNode = 6, .dstPin = 1});
    graph.connections.push_back({.srcNode = 6, .dstNode = 7, .dstPin = 0});
    // remap sin from -1..1 (inMin connected, inMax/outMin/outMax default) into 0..1
    graph.connections.push_back({.srcNode = 7, .dstNode = 9, .dstPin = 0});
    graph.connections.push_back({.srcNode = 8, .dstNode = 9, .dstPin = 1});
    // mix the two colors by the band, feed albedo, and derive roughness from its luminance
    graph.connections.push_back({.srcNode = 10, .dstNode = 12, .dstPin = 0});
    graph.connections.push_back({.srcNode = 11, .dstNode = 12, .dstPin = 1});
    graph.connections.push_back({.srcNode = 9, .dstNode = 12, .dstPin = 2});
    graph.connections.push_back({.srcNode = 12, .dstNode = 13, .dstPin = 0});
    graph.connections.push_back({.srcNode = 12, .dstNode = 14, .dstPin = 0});
    graph.connections.push_back({.srcNode = 13, .dstNode = 14, .dstPin = 2});
    graph.outputNodeId = 14;
    return graph;
}

TestLayer::~TestLayer()
{
    onDetach();
}

void TestLayer::onAttach()
{

    Rapture::RP_INFO("TestLayer attached");

    // Register for scene activation events - store the ID for cleanup
    m_sceneActivatedListenerId = Rapture::GameEvents::onSceneActivated().addListener([this](Rapture::Scene &scene) {
        Rapture::RP_INFO("TestLayer::onSceneActivated - New active scene: {0}", scene.getSceneName());
        onNewActiveScene(scene);
    });

    // Check if a scene is already active when the layer is attached
    // This handles the case where the initial scene is set before this layer's listener is registered
    auto currentActiveScene = Rapture::Application::getInstance().getProject().getActiveScene();
    if (currentActiveScene) {
        Rapture::RP_INFO("TestLayer::onAttach - Initial scene already active: {0}", currentActiveScene->getSceneName());
        onNewActiveScene(*currentActiveScene);
    }

    // Initialize FPS counter variables
    m_fpsCounter = 0;
    m_fpsTimer = 0.0f;
}

void TestLayer::onNewActiveScene(Rapture::Scene &scene)
{
    auto &activeScene = scene;

    // Get project paths
    auto &app = Rapture::Application::getInstance();
    auto &project = app.getProject();
    auto rootPath = project.getProjectRootDirectory();

    // Load Sponza model
    // Disabled: models are now imported from the UI via the file browser / import panel.

    auto sponzaPath = rootPath / "assets/models/glTF2.0/Sponza/Sponza.gltf";
    if (std::filesystem::exists(sponzaPath)) {
        Rapture::RP_INFO("Loading Sponza scene from: {}", sponzaPath.string());
        auto loader = Rapture::glTF2Loader(sponzaPath);
        loader.load(&activeScene);
    } else {
        Rapture::RP_WARN("Sponza model not found at: {}", sponzaPath.string());

        // Fallback: Create a simple test cube if Sponza not found
        auto cube = activeScene.createCube("Test Cube");
        cube.getComponent<Rapture::TransformComponent>().transforms.setTranslation(glm::vec3(0.0f, 0.0f, 0.0f));
        cube.addComponent<Rapture::BLASComponent>(cube.getComponent<Rapture::MeshComponent>().mesh);
        activeScene.registerBLAS(cube);

        // Create a floor
        auto floor = activeScene.createCube("Floor");
        floor.getComponent<Rapture::TransformComponent>().transforms.setTranslation(glm::vec3(0.0f, -1.5f, 0.0f));
        floor.getComponent<Rapture::TransformComponent>().transforms.setScale(glm::vec3(10.0f, 0.1f, 10.0f));
        floor.addComponent<Rapture::BLASComponent>(floor.getComponent<Rapture::MeshComponent>().mesh);
        activeScene.registerBLAS(floor);
    }

    // Graph material test spheres - two different generated graphs, so evalSurfaceGraph dispatch is visible
    {
        auto &graphManager = Rapture::MaterialManager::getSurfaceGraphManager();

        uint32_t graph0Id = graphManager.registerGraph(s_buildFractTintGraph());
        uint32_t graph1Id = graphManager.registerGraph(s_buildSineBandGraph());
        graphManager.writeGeneratedFile(project.getProjectShaderDirectory() / "glsl/generated/SurfaceGraphs.glsl");

        auto spawnGraphSphere = [&](const std::string &name, const glm::vec3 &position, uint32_t graphId) {
            auto sphere = activeScene.createSphere(name);
            auto &transform = sphere.getComponent<Rapture::TransformComponent>();
            transform.transforms.setTranslation(position);
            transform.transforms.setScale(glm::vec3(2.0f));

            auto baseMaterial = Rapture::MaterialManager::getMaterial("Default Material");
            auto mat = std::make_unique<Rapture::MaterialInstance>(baseMaterial, name);
            mat->setGraph(graphId, graphManager.getDefaults(graphId), graphManager.getTextureRefs(graphId));

            auto matRef = Rapture::AssetManager::registerVirtualAsset(std::move(mat), name, Rapture::AssetType::MATERIAL);
            sphere.setComponent<Rapture::MaterialComponent>(matRef);
        };

        spawnGraphSphere("Graph Sphere 0", glm::vec3(-3.0f, 2.0f, 0.0f), graph0Id);
        spawnGraphSphere("Graph Sphere 1", glm::vec3(3.0f, 2.0f, 0.0f), graph1Id);
    }

    // Create a spot light with shadow mapping (inside Sponza courtyard)
    // Note: Rotation is in RADIANS
    Rapture::Entity spotLight = activeScene.createSphere("Spot Light");
    spotLight.setComponent<Rapture::TransformComponent>(glm::vec3(2.0f, 2.0f, -3.0f),   // Position in Sponza
                                                        glm::vec3(-2.243f, 0.0f, 0.0f), // Point downward (radians, ~-128 degrees)
                                                        glm::vec3(0.2f)                 // Small visual scale
    );
    auto &spotLightComp = spotLight.addComponent<Rapture::SpotLightComponent>(glm::vec3(1.0f, 1.0f, 1.0f), // White color
                                                                              1.2f,                        // Intensity
                                                                              15.0f,                       // Range
                                                                              30.0f, // Inner cone angle (degrees)
                                                                              45.0f  // Outer cone angle (degrees)
    );
    spotLightComp.castsShadow = false;
    spotLight.addComponent<Rapture::ShadowComponent>(1028.0f, 1028.0f);

    // Create a directional light with CSM (sun) - pointing down into Sponza
    // Note: Rotation is in RADIANS
    Rapture::Entity sunLight = activeScene.createEntity("Sun");
    sunLight.addComponent<Rapture::TransformComponent>(glm::vec3(-2.0f, 5.0f, -3.0f),  // Position
                                                       glm::vec3(-1.874f, 0.0f, 0.0f), // Point downwards
                                                       glm::vec3(0.2f));
    auto &sunLightComp = sunLight.addComponent<Rapture::DirectionalLightComponent>(glm::vec3(1.0f, 1.0f, 1.0f), // White sunlight
                                                                                   3.14f                        // Intensity
    );
    sunLightComp.castsShadow = true;
    sunLightComp.atmosphereSunLight = true;
    sunLight.addComponent<Rapture::CascadedShadowComponent>(2048.0f, 2048.0f, 4, 0.8f);

    // Create environment entity with a procedurally generated atmosphere skybox
    {
        auto envEntity = activeScene.environment();
        auto &sky = envEntity.addComponent<Rapture::SkyboxComponent>();
        sky.skyIntensity = 0.1f;
        sky.useAtmosphereSkybox = true;
        auto atmo = envEntity.addComponent<Rapture::AtmosphereComponent>();
        atmo.timeOfDay = 11.551f;
        atmo.latitude = -16.069f;
        atmo.longitude = -9.273f;
    }

    {
        Rapture::ProceduralTextureConfig config;
        config.name = "test_white_noise";
        auto noiseTexture = Rapture::ProceduralTexture::generateWhiteNoise(12345, config);
        if (noiseTexture) {
            Rapture::RP_INFO("Generated white noise texture: {}", config.name);
        }
    }

    // Generate a flat atmospheric scattering preview texture
    {
        Rapture::ProceduralTextureConfig config;
        config.name = "test_atmosphere";
        config.format = Rapture::TextureFormat::RGBA16F;
        config.srgb = false;

        auto atmosphereTexture = Rapture::ProceduralTexture::generateAtmosphere(12.0f, nullptr, config);
        if (atmosphereTexture) {
            Rapture::RP_INFO("Generated atmospheric scattering texture: {}", config.name);
        }
    }

    constexpr float chunkSize = 64.0f;
    constexpr int32_t chunkRadius = 6;
    constexpr uint32_t chunkGridSize = (2 * chunkRadius + 1) * (2 * chunkRadius + 1);
    constexpr float terrainExtent = chunkSize * (2 * chunkRadius + 1);

    Rapture::TerrainConfig terrainConfig = {};
    terrainConfig.chunkWorldSize = chunkSize;
    terrainConfig.heightScale = 40.0f;
    terrainConfig.terrainWorldSize = terrainExtent;
    terrainConfig.chunkGridSize = chunkGridSize;
    terrainConfig.hmType = Rapture::HM_CEPV;

    auto terrainEntity = activeScene.createEntity("Terrain");
    auto &terrainComp = terrainEntity.addComponent<Rapture::TerrainComponent>(terrainConfig);
    terrainComp.isEnabled = true;
    Rapture::RP_INFO("Terrain entity created with {} chunks (radius {})", terrainComp.generator->getChunkCount(),
                     terrainConfig.getChunkRadius());

    Rapture::RP_INFO("Scene setup complete for: {}", activeScene.getSceneName());
}

void TestLayer::onDetach()
{
    if (m_sceneActivatedListenerId != 0) {
        Rapture::GameEvents::onSceneActivated().removeListener(m_sceneActivatedListenerId);
        m_sceneActivatedListenerId = 0;
    }
}

void TestLayer::notifyCameraChange() {}

void TestLayer::onUpdate(float ts)
{

    RAPTURE_PROFILE_SCOPE("TestLayer::onUpdate");
    // Get the active scene from the project
    auto activeScene = Rapture::Application::getInstance().getProject().getActiveScene();
    if (!activeScene) return;

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
}
