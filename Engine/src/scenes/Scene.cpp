#include "Scene.h"
#include "entities/Entity.h"

#include "components/Components.h"
#include "components/RigidBodyComponent.h"
#include "components/TerrainComponent.h"
#include "scenes/instances/DirectionalLight3D.h"
#include "scenes/instances/Environment.h"
#include "scenes/instances/StaticMesh3D.h"
#include "renderer/SceneRenderData.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"
#include "scenes/instances/Instance.h"

#include "asset_manager/AssetManager.h"
#include "asset_manager/ReservedAssets.h"
#include "logging/TracyProfiler.h"
#include "physics/PhysicsSystem.h"
#include "scenes/entities/EntityCommon.h"
#include "serialization/SerialDocument.h"
#include "window_context/Application.h"

#include <memory>
#include <unordered_map>

namespace Rapture {

static constexpr float DEFAULT_SUN_PITCH = -1.874f;
static constexpr float DEFAULT_SUN_INTENSITY = 3.14f;
static constexpr float DEFAULT_SKY_INTENSITY = 0.1f;
static constexpr float DEFAULT_FLOOR_SIZE = 20.0f;

static constexpr std::string_view KEY_FORMAT_VERSION = "formatVersion";
static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_FRUSTUM_CULLING = "frustumCulling";
static constexpr std::string_view KEY_MAIN_CAMERA = "mainCamera";
static constexpr std::string_view KEY_INSTANCES = "instances";

Scene::Scene(const std::string &sceneName)
{
    m_config.sceneName = sceneName;

    auto &app = Application::getInstance();
    m_renderData = std::make_unique<SceneRenderData>(app.getVulkanContext().getRenderContext(), *this, app.getFramesInFlight());

    m_root = std::make_unique<Instance>(*this, "Root");
    m_root->add<Environment>("Environment");

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

    entity.addComponent<MeshComponent>(AssetManager::getAsset(RE_PRIMITIVE_CUBE_MESH), mobility);

    // Add a material
    auto materialRef = AssetManager::importDefaultAsset(AssetType::MATERIAL_INSTANCE);
    if (materialRef) {
        entity.addComponent<MaterialComponent>(materialRef);
    }

    return entity;
}

void Scene::addDefaultContent()
{
    auto *sun = m_root->add<DirectionalLight3D>("Sun");
    sun->setRotation(glm::vec3(DEFAULT_SUN_PITCH, 0.0f, 0.0f));
    sun->setColor(glm::vec3(1.0f));
    sun->setIntensity(DEFAULT_SUN_INTENSITY);
    sun->setAtmosphereSun(true);
    sun->setCastsShadow(true);

    auto *floor = m_root->add<StaticMesh3D>("Floor");
    floor->setMesh(RE_PRIMITIVE_PLANE_MESH);
    floor->setMaterial(RE_DEFAULT_MATERIAL_INSTANCE);
    floor->setScale(glm::vec3(DEFAULT_FLOOR_SIZE, 1.0f, DEFAULT_FLOOR_SIZE));

    if (m_environment != nullptr) {
        m_environment->setUsesAtmosphereSkybox(true);
        m_environment->setSkyIntensity(DEFAULT_SKY_INTENSITY);
    }
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

    entity.addComponent<MeshComponent>(AssetManager::getAsset(RE_PRIMITIVE_SPHERE_MESH), mobility);

    // Add a material
    auto materialRef = AssetManager::importDefaultAsset(AssetType::MATERIAL_INSTANCE);
    if (materialRef) {
        entity.addComponent<MaterialComponent>(materialRef);
    }

    return entity;
}

void Scene::destroyEntity(Entity entity)
{
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

    if (m_environment != nullptr) {
        m_environment->update();
    }

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
            auto *meshComp = entity.tryGetComponent<MeshComponent>();
            if (meshComp != nullptr && meshComp->mesh) {
                BoundingBox bounds(meshComp->mesh->getBoundsMin(), meshComp->mesh->getBoundsMax());
                glm::vec3 halfExtents = bounds.getExtents() * 0.5f * transform->scale();
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

    node.set(KEY_FORMAT_VERSION, static_cast<uint64_t>(SCENE_FORMAT_VERSION));
    node.set(KEY_NAME, std::string_view(m_config.sceneName));
    node.set(KEY_FRUSTUM_CULLING, m_config.frustumCullingEnabled);

    std::unordered_map<EntityID, uint32_t> indices;
    uint32_t nextIndex = 0;
    s_indexSubtree(m_root.get(), indices, nextIndex);

    Entity mainCamera = getMainCamera();
    if (mainCamera.isValid()) {
        auto it = indices.find(mainCamera.getID());
        if (it != indices.end()) {
            node.set(KEY_MAIN_CAMERA, static_cast<uint64_t>(it->second));
        }
    }

    WriteNode instances = node.addArray(KEY_INSTANCES);
    for (const auto &child : m_root->children()) {
        child->serialize(instances.appendObject());
    }
}

void Scene::clearInstances()
{
    while (!m_root->children().empty()) {
        m_root->removeChild(m_root->children().front().get());
    }
}

std::unique_ptr<Scene> Scene::deserialize(ReadNode node)
{
    RAPTURE_PROFILE_FUNCTION();

    uint32_t formatVersion = static_cast<uint32_t>(node.child(KEY_FORMAT_VERSION).asU64(0));
    if (formatVersion != SCENE_FORMAT_VERSION) {
        RP_CORE_ERROR("cannot read a version {} scene, this build reads version {}", formatVersion, SCENE_FORMAT_VERSION);
        return nullptr;
    }

    auto scene = std::make_unique<Scene>(std::string(node.child(KEY_NAME).asString("Untitled Scene")));
    scene->m_config.frustumCullingEnabled = node.child(KEY_FRUSTUM_CULLING).asBool(scene->m_config.frustumCullingEnabled);

    // the constructor seeds a default environment, which the document supplies again
    scene->clearInstances();

    std::vector<Instance *> order;
    ReadNode instances = node.child(KEY_INSTANCES);
    for (size_t i = 0; i < instances.size(); i++) {
        if (!Instance::loadSubtree(*scene->m_root, instances.at(i), order)) {
            RP_CORE_ERROR("scene '{}' could not be read", scene->m_config.sceneName);
            return nullptr;
        }
    }

    ReadNode mainCamera = node.child(KEY_MAIN_CAMERA);
    if (mainCamera.valid()) {
        size_t index = static_cast<size_t>(mainCamera.asU64(order.size()));
        if (index < order.size()) {
            scene->setMainCamera(order[index]->entity());
        } else {
            RP_CORE_WARN("scene '{}' names a main camera that is not in its tree", scene->m_config.sceneName);
        }
    }

    return scene;
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
