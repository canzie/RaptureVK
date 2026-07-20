#include "Scene.h"
#include "entities/Entity.h"

#include "components/Components.h"
#include "components/TerrainComponent.h"
#include "components/systems/Environment.h"
#include "renderer/SceneRenderData.h"

#include "asset_manager/AssetManager.h"
#include "logging/TracyProfiler.h"
#include "meshes/MeshPrimitives.h"
#include "render_targets/swap_chains/SwapChain.h"
#include "window_context/Application.h"

#include <memory>

namespace Rapture {

Scene::Scene(const std::string &sceneName)
{
    m_config.sceneName = sceneName;

    auto &app = Application::getInstance();
    uint32_t frameCount = app.getMainWindow().getSwapChain()->getImageCount();
    m_renderData = std::make_unique<SceneRenderData>(app.getVulkanContext().getRenderContext(), *this, frameCount);

    m_environment = std::make_unique<Environment>(createEntity("Environment"));
}

Scene::~Scene()
{
    // EnTT registry will automatically clean up all entities and components
}

Entity Scene::createEntity(const std::string &name)
{
    if (locked) {
        RP_CORE_WARN("Cannot create entity in locked scene '{}'", getSceneName());
        return Entity::null();
    }

    // Create entity in the registry
    entt::entity handle = m_registry.create();

    // Create Entity wrapper
    Entity entity(handle, this);

    // Add basic name component if you have one
    entity.addComponent<TagComponent>(name);

    return entity;
}

Entity Scene::createCube(const std::string &name)
{
    if (locked) {
        RP_CORE_WARN("Cannot create entity in locked scene '{}'", getSceneName());
        return Entity::null();
    }

    // Create entity in the registry
    entt::entity handle = m_registry.create();

    // Create Entity wrapper
    Entity entity(handle, this);

    // Add basic name component if you have one
    entity.addComponent<TagComponent>(name);

    entity.addComponent<TransformComponent>(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));

    // Add a cube mesh
    auto cubeMesh = std::make_unique<Mesh>(Primitives::CreateCube());
    auto meshRef = AssetManager::registerVirtualAsset(std::move(cubeMesh), "Primitive_Cube_" + name, AssetType::MESH);
    entity.addComponent<MeshComponent>(meshRef);

    entity.addComponent<BoundingBoxComponent>(glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f));

    // Add a material
    auto materialRef = AssetManager::importDefaultAsset(AssetType::MATERIAL_INSTANCE);
    if (materialRef) {
        entity.addComponent<MaterialComponent>(materialRef);
    }

    return entity;
}

Entity Scene::createSphere(const std::string &name)
{
    if (locked) {
        RP_CORE_WARN("Cannot create entity in locked scene '{}'", getSceneName());
        return Entity::null();
    }

    // Create entity in the registry
    entt::entity handle = m_registry.create();

    // Create Entity wrapper
    Entity entity(handle, this);

    // Add basic name component if you have one
    entity.addComponent<TagComponent>(name);

    entity.addComponent<TransformComponent>(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));

    // Add a sphere mesh
    auto sphereMesh = std::make_unique<Mesh>(Primitives::CreateSphere(1.0f, 32));
    auto meshRef = AssetManager::registerVirtualAsset(std::move(sphereMesh), "Primitive_Sphere_" + name, AssetType::MESH);
    entity.addComponent<MeshComponent>(meshRef);

    entity.addComponent<BoundingBoxComponent>(glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f));

    // Add a material
    auto materialRef = AssetManager::importDefaultAsset(AssetType::MATERIAL_INSTANCE);
    if (materialRef) {
        entity.addComponent<MaterialComponent>(materialRef);
    }

    return entity;
}

void Scene::destroyEntity(Entity entity)
{
    if (entity.isValid() && entity == m_environment->getEntity()) {
        RP_CORE_WARN("The environment entity cannot be destroyed");
        return;
    }

    if (entity.isValid() && entity.getScene() == this) {
        auto *blasComp = entity.tryGetComponent<BLASComponent>();
        if (blasComp != nullptr) {
            if (m_tlas != nullptr) {
                m_tlas->removeInstance(entity.getID());
            }
            if (blasComp->blas != nullptr) {
                ensureBLASFreeBuckets();
                m_blasFreeBuckets[m_blasFreeBucket].push_back(std::move(blasComp->blas));
            }
            m_tlasDirty = true;
        }
        m_registry.destroy(entity.getHandle());
    }
}

void Scene::onUpdate(float dt)
{
    (void)dt;

    // Get current frame dimensions for camera updates
    auto &app = Application::getInstance();
    auto swapChain = app.getMainWindow().getSwapChain();
    uint32_t frameCounter = app.getFrameInFlightIndex();

    {
        RAPTURE_PROFILE_SCOPE("OldPerEntity::updateMeshes");
        auto meshView = m_registry.view<TransformComponent, MeshComponent, MaterialComponent, TagComponent>();
        for (auto entity : meshView) {
            auto [transform, mesh, material, tag] =
                meshView.get<TransformComponent, MeshComponent, MaterialComponent, TagComponent>(entity);

            material.material->updatePendingTextures();
        }
    }

    glm::vec3 cameraPosition = glm::vec3(0.0f);
    Frustum *frustum = nullptr;
    Entity mainCamera = getMainCamera();
    if (mainCamera.isValid()) {
        auto [cameraTransform, cameraComponent] = mainCamera.tryGetComponents<TransformComponent, CameraComponent>();
        if (cameraTransform && cameraComponent) {
            cameraPosition = cameraTransform->translation();
            frustum = &cameraComponent->frustum;
        }
    }

    auto terrainView = m_registry.view<TerrainComponent>();
    for (auto entity : terrainView) {
        auto &terrain = terrainView.get<TerrainComponent>(entity);
        if (terrain.generator && terrain.isEnabled && terrain.generator->isInitialized()) {
            terrain.generator->update(cameraPosition, *frustum, frameCounter);
        }
    }

    m_environment->update();

    // Update regular shadow maps
    auto shadowView = m_registry.view<TransformComponent, ShadowComponent>();
    for (auto entityHandle : shadowView) {
        Entity entity(entityHandle, this);
        auto *light = Light_tryGetLight(entity);
        if (light == nullptr) {
            continue;
        }
        auto [transform, shadow] = shadowView.get<TransformComponent, ShadowComponent>(entityHandle);

        if (shadow.shadowMap && shadow.isActive && shadow.needsUpdate(*light, transform)) {

            // Update the shadow map view matrix
            shadow.shadowMap->updateViewMatrix(entity, transform, cameraPosition);
        }
    }

    // Update cascaded shadow maps
    auto cascadedShadowView = m_registry.view<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>();
    for (auto entity : cascadedShadowView) {
        auto [light, transform, shadow] =
            cascadedShadowView.get<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>(entity);

        if (shadow.cascadedShadowMap && shadow.isActive) {
            // Update the cascaded shadow map view matrices
            Entity mainCamera = getMainCamera();
            if (mainCamera.isValid()) {
                auto cameraComp = mainCamera.tryGetComponent<CameraComponent>();
                if (cameraComp) {
                    shadow.cascadedShadowMap->updateViewMatrix(light, transform, *cameraComp);
                }
            }
        }
    }

    m_renderData->onUpdate(frameCounter);

    ensureBLASFreeBuckets();
    m_blasFreeBucket = (m_blasFreeBucket + 1) % m_blasFreeBuckets.size();
    m_blasFreeBuckets[m_blasFreeBucket].clear();

    if (m_tlasDirty) {
        if (m_tlas != nullptr && m_tlas->getInstanceCount() > 0) {
            m_tlas->build();
        }
        m_tlasDirty = false;
    }

    updateTLAS();
}

SceneSettings &Scene::getSettings()
{
    return m_config;
}

const SceneSettings &Scene::getSettings() const
{
    return m_config;
}

std::string Scene::getSceneName() const
{
    return m_config.sceneName;
}

void Scene::setMainCamera(Entity camera)
{
    if (!camera.isValid() || !camera.hasComponent<CameraComponent>()) {
        return;
    }

    // Mark camera as main camera via component flag
    camera.getComponent<CameraComponent>().isMainCamera = true;

    // Unmark any other cameras
    auto view = m_registry.view<CameraComponent>();
    for (auto entity : view) {
        if (Entity(entity, this) != camera) {
            view.get<CameraComponent>(entity).isMainCamera = false;
        }
    }
}

Entity Scene::getMainCamera() const
{
    // Query for camera with isMainCamera flag
    auto view = m_registry.view<CameraComponent>();
    for (auto entity : view) {
        if (view.get<CameraComponent>(entity).isMainCamera) {
            return Entity(entity, const_cast<Scene *>(this));
        }
    }
    return Entity::null();
}

Entity Scene::environmentEntity() const
{
    return m_environment->getEntity();
}

void Scene::registerBLAS(Entity &entity)
{

    if (!m_tlas) {
        m_tlas = std::make_unique<TLAS>();
    }

    auto [blas, mesh, transform] = entity.tryGetComponents<BLASComponent, MeshComponent, TransformComponent>();
    if (!blas || !mesh || !transform) {
        RP_CORE_ERROR("Entity does not have a valid BLAS component");
        return;
    }

    TLASInstance instance;
    instance.blas = blas->blas.get();
    instance.transform = transform->transformMatrix();
    instance.entityID = entity.getID();
    m_tlas->addInstance(instance);
    m_tlasDirty = true;
}

void Scene::buildTLAS()
{
    if (!m_tlas) {
        RP_CORE_ERROR("TLAS is not initialized");
        return;
    }

    m_tlas->build();
}

void Scene::ensureBLASFreeBuckets()
{
    uint32_t framesInFlight = Application::getInstance().getMainWindow().getSwapChain()->getImageCount();
    size_t bucketCount = static_cast<size_t>(framesInFlight) + 1;
    if (m_blasFreeBuckets.size() < bucketCount) {
        m_blasFreeBuckets.resize(bucketCount);
    }
}

// TODO: update this so we update the transform directly instead of sotring the change and letting the tlas go over it again
void Scene::updateTLAS()
{

    if (!m_tlas) {
        return;
    }

    std::vector<std::pair<uint32_t, glm::mat4>> instanceUpdates;
    auto &instances = m_tlas->getInstances();
    int instanceIndex = 0;

    for (auto &instance : instances) {
        auto entity = Entity(instance.entityID, this);

        if (entity.isValid()) {
            auto &transform = entity.getComponent<TransformComponent>();
            generation_t gen = transform.getGeneration();
            if (gen != instance.lastTransformGeneration) {
                instance.lastTransformGeneration = gen;
                instanceUpdates.push_back({instanceIndex, transform.transformMatrix()});
            }
        }
        instanceIndex++;
    }

    m_tlas->updateInstances(instanceUpdates);
}
} // namespace Rapture
