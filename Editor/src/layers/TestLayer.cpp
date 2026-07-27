#include "TestLayer.h"

#include "components/Components.h"
#include "generators/terrain/TerrainTypes.h"
#include "logging/Log.h"
#include "renderer/DeferredRenderer.h"
#include "scenes/Scene.h"
#include "scenes/SceneManager.h"
#include "scenes/entities/EntityCommon.h"
#include "utils/Timestep.h"
#include "window_context/Application.h"

#include "loaders/gltf/glTFLoader.h"

#include <filesystem>

#include "logging/TracyProfiler.h"

#include "asset_manager/AssetManager.h"
#include "components/RigidBodyComponent.h"
#include "components/TerrainComponent.h"
#include "generators/textures/ProceduralTextures.h"
#include "materials/Material.h"
#include "materials/MaterialInstance.h"
#include "materials/graph/SurfaceGraphManager.h"
#include "utils/Timestep.h"

/**
 * @brief Graph 0: smooth silver metal, a clean surface to read the DDGI specular reflection
 * @return The authored graph
 */
static Rapture::MaterialGraph s_buildSilverMetalGraph()
{
    using GN = Rapture::GraphNodeType;
    Rapture::MaterialGraph graph;
    graph.name = "Graph0";
    // Pins: albedo(0), normal(1), roughness(2), metallic(3). Albedo tints the reflection for a metal.
    graph.nodes.push_back({.id = 1,
                           .type = GN::SURFACE_OUTPUT,
                           .inputValues = {Rapture::PinValue(glm::vec3(0.95f, 0.94f, 0.90f)), std::nullopt,
                                           Rapture::PinValue(0.04f), Rapture::PinValue(1.0f)}});
    graph.outputNodeId = 1;
    return graph;
}

/**
 * @brief Graph 0b: same silver metal, but at the roughness where SSR should be handing off to
 * the DDGI-approximated specular (below this, SSR should carry the reflection instead)
 * @return The authored graph
 */
static Rapture::MaterialGraph s_buildSatinMetalGraph()
{
    using GN = Rapture::GraphNodeType;
    Rapture::MaterialGraph graph;
    graph.name = "Graph0b";
    graph.nodes.push_back({.id = 1,
                           .type = GN::SURFACE_OUTPUT,
                           .inputValues = {Rapture::PinValue(glm::vec3(0.95f, 0.94f, 0.90f)), std::nullopt,
                                           Rapture::PinValue(0.35f), Rapture::PinValue(1.0f)}});
    graph.outputNodeId = 1;
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

    RP_INFO("TestLayer attached");

    // Register for scene activation events - store the ID for cleanup
    m_sceneActivatedListenerId = Rapture::GameEvents::onSceneActivated().addListener([this](Rapture::Scene &scene) {
        RP_INFO("TestLayer::onSceneActivated - New active scene: {0}", scene.getSceneName());
        onNewActiveScene(scene);
    });

    // Bootstrap the default scene when the layer is attached
    // This handles the case where the initial scene is set before this layer's listener is registered
    Rapture::Scene *defaultScene =
        Rapture::Application::getInstance().getProject().getSceneManager().getScene(RAPTURE_DEFAULT_SCENE_NAME);
    if (defaultScene != nullptr) {
        onNewActiveScene(*defaultScene);
    }

    // Initialize FPS counter variables
    m_fpsCounter = 0;
    m_fpsTimer = 0.0f;
}

void TestLayer::onNewActiveScene(Rapture::Scene &scene)
{
    if (scene.getSceneName() != RAPTURE_DEFAULT_SCENE_NAME) {
        return;
    }

    auto &activeScene = scene;

    // Get project paths
    auto &app = Rapture::Application::getInstance();
    auto &project = app.getProject();

    // Models are imported from the UI now; the registration pass picks up their .rasset files at startup
    auto cube = activeScene.createCube("Test Cube");
    cube.getComponent<Rapture::TransformComponent>().transforms.setTranslation(glm::vec3(0.0f, 5.0f, 0.0f));
    cube.addComponent<Rapture::BLASComponent>(cube.getComponent<Rapture::MeshComponent>().mesh);
    activeScene.registerBLAS(cube);

    auto floor = activeScene.createCube("Floor", Rapture::MOBILITY_DYNAMIC);
    floor.getComponent<Rapture::TransformComponent>().transforms.setTranslation(glm::vec3(0.0f, 0.0f, 0.0f));
    floor.getComponent<Rapture::TransformComponent>().transforms.setScale(glm::vec3(10.0f, 0.1f, 10.0f));
    floor.addComponent<Rapture::BLASComponent>(floor.getComponent<Rapture::MeshComponent>().mesh);
    activeScene.registerBLAS(floor);
    floor.addComponent<Rapture::RigidBodyComponent>().motionType = Rapture::PHYSICS_MOTION_STATIC;

    // Graph material test spheres - two different generated graphs, so evalSurfaceGraph dispatch is visible
    {
        auto &graphManager = Rapture::MaterialManager::getSurfaceGraphManager();

        uint32_t graph0Id = graphManager.registerGraph(s_buildSilverMetalGraph());
        uint32_t graph0bId = graphManager.registerGraph(s_buildSatinMetalGraph());
        uint32_t graph1Id = graphManager.registerGraph(s_buildSineBandGraph());

        auto spawnGraphSphere = [&](const std::string &name, const glm::vec3 &position, uint32_t graphId) {
            auto sphere = activeScene.createSphere(name, Rapture::MOBILITY_DYNAMIC);
            auto &transform = sphere.getComponent<Rapture::TransformComponent>();
            transform.transforms.setTranslation(position);
            transform.transforms.setScale(glm::vec3(2.0f));

            auto baseMaterial = Rapture::MaterialManager::getMaterial("Default Material");
            auto mat = std::make_unique<Rapture::MaterialInstance>(baseMaterial, name);
            mat->setGraph(graphId, graphManager.getDefaults(graphId), graphManager.getTextureRefs(graphId));

            auto matRef = Rapture::AssetManager::registerVirtualAsset(std::move(mat), name, Rapture::AssetType::MATERIAL_INSTANCE);
            sphere.setComponent<Rapture::MaterialComponent>(matRef);
            if (graphId == graph0Id) {
                cube.setComponent<Rapture::MaterialComponent>(matRef);
            }
            sphere.addComponent<Rapture::BLASComponent>(sphere.getComponent<Rapture::MeshComponent>().mesh);
            activeScene.registerBLAS(sphere);
            sphere.addComponent<Rapture::RigidBodyComponent>();
        };

        spawnGraphSphere("Graph Sphere 0", glm::vec3(-3.0f, 10.0f, 0.0f), graph0Id);
        spawnGraphSphere("Graph Sphere 0b", glm::vec3(0.0f, 10.0f, -3.0f), graph0bId);
        spawnGraphSphere("Graph Sphere 1", glm::vec3(3.0f, 10.0f, 0.0f), graph1Id);
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
        auto envEntity = activeScene.environmentEntity();
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
            RP_INFO("Generated white noise texture: {}", config.name);
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
            RP_INFO("Generated atmospheric scattering texture: {}", config.name);
        }
    }

    constexpr float chunkSize = 64.0f;
    constexpr int32_t chunkRadius = 10;
    constexpr uint32_t chunkGridSize = (2 * chunkRadius + 1) * (2 * chunkRadius + 1);
    constexpr float terrainExtent = chunkSize * (2 * chunkRadius + 1);

    Rapture::TerrainConfig terrainConfig = {};
    terrainConfig.chunkWorldSize = chunkSize;
    terrainConfig.heightScale = 70.0f;
    terrainConfig.terrainWorldSize = terrainExtent;
    terrainConfig.chunkGridSize = chunkGridSize;
    terrainConfig.hmType = Rapture::HM_CEPV;

    auto terrainEntity = activeScene.createEntity("Terrain");
    auto &terrainComp = terrainEntity.addComponent<Rapture::TerrainComponent>(terrainConfig);
    terrainComp.isEnabled = true;
    RP_INFO("Terrain entity created with {} chunks (radius {})", terrainComp.generator->getChunkCount(),
            terrainConfig.getChunkRadius());

    Rapture::MaterialManager::getSurfaceGraphManager().writeGeneratedFiles(project.getProjectShaderDirectory() / "glsl/generated");

    RP_INFO("Scene setup complete for: {}", activeScene.getSceneName());
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

    // Update FPS counter
    m_fpsCounter++;
    m_fpsTimer += ts;

    // Log FPS approximately once per second
    if (m_fpsTimer >= 1.0f) {
        float fps = static_cast<float>(m_fpsCounter) / m_fpsTimer;

        const Rapture::Telemetry &telemetry = Rapture::Application::getInstance().getTelemetry();
        double vramUsedMb = static_cast<double>(telemetry.vramUsedBytes) / (1024.0 * 1024.0);
        double vramBudgetMb = static_cast<double>(telemetry.vramBudgetBytes) / (1024.0 * 1024.0);
        double vramPercent = telemetry.vramBudgetBytes > 0 ? (vramUsedMb / vramBudgetMb) * 100.0 : 0.0;
        double ramUsedMb = static_cast<double>(telemetry.ramUsedBytes) / (1024.0 * 1024.0);
        RP_INFO("FPS: {0:.1f}  VRAM: {1:.0f}/{2:.0f} MB ({3:.1f}%)  RAM: {4:.0f} MB", fps, vramUsedMb, vramBudgetMb, vramPercent,
                ramUsedMb);

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
