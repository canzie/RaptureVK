#include "Scene.h"
#include "entities/Entity.h"

#include "components/Components.h"
#include "components/RigidBodyComponent.h"
#include "components/TerrainComponent.h"
#include "components/systems/Environment.h"
#include "renderer/SceneRenderData.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"
#include "scenes/instances/Instance.h"

#include "asset_manager/AssetManager.h"
#include "logging/TracyProfiler.h"
#include "meshes/MeshPrimitives.h"
#include "physics/PhysicsSystem.h"
#include "scenes/entities/EntityCommon.h"
#include "serialization/SerialDocument.h"
#include "window_context/Application.h"

#include <fstream>
#include <memory>
#include <unordered_map>

namespace Rapture {

Scene::Scene(const std::string &sceneName)
{
    m_config.sceneName = sceneName;

    auto &app = Application::getInstance();
    m_renderData = std::make_unique<SceneRenderData>(app.getVulkanContext().getRenderContext(), *this, app.getFramesInFlight());

    m_root = std::make_unique<Instance>(*this, "Root");

    m_environment = std::make_unique<Environment>(createEntity("Environment"));

    m_physics = std::make_unique<PhysicsSystem>();
    m_registry.on_construct<RigidBodyComponent>().connect<&Scene::onRigidBodyConstructed>(this);
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

Entity Scene::createCube(const std::string &name, Mobility mobility)
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
    entity.addComponent<MeshComponent>(meshRef, mobility);

    entity.addComponent<BoundingBoxComponent>(glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f));

    // Add a material
    auto materialRef = AssetManager::importDefaultAsset(AssetType::MATERIAL_INSTANCE);
    if (materialRef) {
        entity.addComponent<MaterialComponent>(materialRef);
    }

    return entity;
}

Entity Scene::createSphere(const std::string &name, Mobility mobility)
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
    entity.addComponent<MeshComponent>(meshRef, mobility);

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
        if (entity.hasComponent<RayTracedComponent>()) {
            if (m_tlas != nullptr) {
                m_tlas->removeInstance(entity.getID());
            }
            m_tlasDirty = true;
        }
        m_registry.destroy(entity.getHandle());
    }
}

void Scene::onUpdate(float dt)
{
    if (m_physics) {
        registerRigidBodies();
        m_physics->onUpdate(dt);
        syncRigidBodyTransforms();
    }

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

        ShadowMap *shadowMap = m_renderData->getShadowMap(entity.getID());
        if (shadowMap != nullptr && shadow.isActive && shadow.needsUpdate(*light, transform)) {

            // Update the shadow map view matrix
            shadowMap->updateViewMatrix(entity, transform, cameraPosition);
        }
    }

    // Update cascaded shadow maps
    auto cascadedShadowView = m_registry.view<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>();
    for (auto entity : cascadedShadowView) {
        auto [light, transform, shadow] =
            cascadedShadowView.get<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>(entity);

        CascadedShadowMap *cascadedShadowMap = m_renderData->getCascadedShadowMap(static_cast<EntityID>(entity));
        if (cascadedShadowMap != nullptr && shadow.isActive) {
            // Update the cascaded shadow map view matrices
            Entity mainCamera = getMainCamera();
            if (mainCamera.isValid()) {
                auto cameraComp = mainCamera.tryGetComponent<CameraComponent>();
                if (cameraComp) {
                    cascadedShadowMap->setLambda(shadow.lambda);
                    cascadedShadowMap->setShadowDistance(shadow.shadowDistance);
                    cascadedShadowMap->updateViewMatrix(light, transform, *cameraComp);
                }
            }
        }
    }

    m_renderData->onUpdate(frameCounter);

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

void Scene::onRigidBodyConstructed(entt::registry &registry, entt::entity entity)
{
    (void)registry;
    m_pendingRigidBodies.push_back(entity);
}

void Scene::registerRigidBodies()
{
    if (m_pendingRigidBodies.empty()) {
        return;
    }

    for (entt::entity handle : m_pendingRigidBodies) {
        if (!m_registry.valid(handle)) {
            continue;
        }

        Entity entity(handle, this);
        auto [rigidBody, transform] = entity.tryGetComponents<RigidBodyComponent, TransformComponent>();
        if (rigidBody == nullptr || transform == nullptr || rigidBody->bodyId.isValid()) {
            continue;
        }

        RigidBodyConfig config;
        config.shape = rigidBody->shape;
        if (rigidBody->shapeFromBounds) {
            if (auto *bounds = entity.tryGetComponent<BoundingBoxComponent>()) {
                glm::vec3 halfExtents = bounds->localBoundingBox.getExtents() * 0.5f * transform->scale();
                config.shape = PhysicsBoxShape{halfExtents};
            }
        }
        config.position = transform->translation();
        config.rotation = transform->transforms.getRotationQuat();
        config.motionType = rigidBody->motionType;
        config.friction = rigidBody->friction;
        config.restitution = rigidBody->restitution;
        config.startActive = rigidBody->startActive;

        rigidBody->bodyId = m_physics->createRigidBody(config, static_cast<uint64_t>(handle));
    }

    m_pendingRigidBodies.clear();
}

void Scene::syncRigidBodyTransforms()
{
    for (const PhysicsBodyState &state : m_physics->getActiveBodyStates()) {
        const entt::entity handle = static_cast<entt::entity>(static_cast<uint32_t>(state.userData));
        if (!m_registry.valid(handle)) {
            continue;
        }

        auto *transform = m_registry.try_get<TransformComponent>(handle);
        if (transform == nullptr) {
            continue;
        }

        transform->transforms.setTranslation(state.position);
        transform->transforms.setRotation(state.rotation);
    }
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

Instance *Scene::instanceFor(Entity entity) const
{
    const auto *ref = entity.tryGetComponent<InstanceComponent>();
    return ref != nullptr ? ref->instance : nullptr;
}

void Scene::destroyInstance(Instance *instance)
{
    if (instance == nullptr || instance == m_root.get()) {
        return;
    }

    Instance *parent = instance->parent();
    if (parent == nullptr) {
        RP_CORE_ERROR("cannot destroy instance '{}' because it is not parented", instance->name());
        return;
    }

    parent->removeChild(instance);
}

// pre-order over the tree, the order a loader recreates the instances in
static void s_indexSubtree(const Instance *instance, std::unordered_map<EntityID, uint32_t> &indices, uint32_t &nextIndex)
{
    for (const auto &child : instance->children()) {
        indices.emplace(child->entity().getID(), nextIndex);
        nextIndex++;
        s_indexSubtree(child.get(), indices, nextIndex);
    }
}

void Scene::serialize(WriteNode node) const
{
    RAPTURE_PROFILE_FUNCTION();

    node.set("formatVersion", static_cast<uint64_t>(SCENE_FORMAT_VERSION));
    node.set("name", std::string_view(m_config.sceneName));
    node.set("frustumCulling", m_config.frustumCullingEnabled);

    std::unordered_map<EntityID, uint32_t> indices;
    uint32_t nextIndex = 0;
    s_indexSubtree(m_root.get(), indices, nextIndex);

    Entity mainCamera = getMainCamera();
    if (mainCamera.isValid()) {
        auto it = indices.find(mainCamera.getID());
        if (it != indices.end()) {
            node.set("mainCamera", static_cast<uint64_t>(it->second));
        }
    }

    WriteNode instances = node.addArray("instances");
    for (const auto &child : m_root->children()) {
        child->serialize(instances.appendObject());
    }
}

bool Scene::writeToFile(const std::filesystem::path &path) const
{
    RAPTURE_PROFILE_FUNCTION();

    SerialDocument doc;
    serialize(doc.root());

    std::string text = doc.toText();
    if (text.empty()) {
        RP_CORE_ERROR("Failed to serialize scene '{}'", m_config.sceneName);
        return false;
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            RP_CORE_ERROR("Failed to create the directory for '{}': {}", path.string(), ec.message());
            return false;
        }
    }

    std::filesystem::path tempPath = path;
    tempPath += ".tmp";

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            RP_CORE_ERROR("Failed to open '{}' for writing", tempPath.string());
            return false;
        }

        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out.good()) {
            RP_CORE_ERROR("Failed to write '{}'", tempPath.string());
            return false;
        }
    }

    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        RP_CORE_ERROR("Failed to move '{}' onto '{}': {}", tempPath.string(), path.string(), ec.message());
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    RP_CORE_INFO("Wrote scene '{}' to '{}'", m_config.sceneName, path.string());
    return true;
}

void Scene::registerBLAS(Entity &entity)
{

    if (!m_tlas) {
        m_tlas = std::make_unique<TLAS>();
    }

    auto [mesh, transform] = entity.tryGetComponents<MeshComponent, TransformComponent>();
    if (!entity.hasComponent<RayTracedComponent>() || mesh == nullptr || transform == nullptr || !mesh->mesh) {
        RP_CORE_ERROR("Entity cannot be ray traced");
        return;
    }

    BLAS *blas = mesh->mesh->getBLAS();
    if (blas == nullptr) {
        RP_CORE_ERROR("Entity's mesh has no acceleration structure");
        return;
    }

    TLASInstance instance;
    instance.blas = blas;
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
