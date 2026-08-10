#include "Scene.h"
#include "entities/Entity.h"

#include "components/Components.h"
#include "components/RigidBodyComponent.h"
#include "components/TerrainComponent.h"
#include "modules/controllers/Controller.h"
#include "scenes/instances/Camera3D.h"
#include "scenes/instances/DirectionalLight3D.h"
#include "scenes/instances/Environment.h"
#include "scenes/instances/StaticMesh3D.h"
#include "renderer/SceneRenderData.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"
#include "scenes/instances/Instance.h"
#include "scenes/instances/InstanceRegistry.h"

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
    auto materialRef = AssetManager::importDefaultAsset(ASSET_MATERIAL_INSTANCE);
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
    auto materialRef = AssetManager::importDefaultAsset(ASSET_MATERIAL_INSTANCE);
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

void Scene::stepPhysics(float dt)
{
    if (m_physics == nullptr) {
        return;
    }

    registerRigidBodies();
    m_physics->onUpdate(dt);
    syncRigidBodyTransforms();
}

void Scene::onUpdate(float dt)
{
    (void)dt;

    if (!active) {
        return;
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
    Camera3D *activeCamera = m_activeController != nullptr ? m_activeController->viewCamera() : nullptr;
    if (activeCamera != nullptr) {
        Entity camera = activeCamera->entity();
        auto [cameraTransform, cameraComponent] = camera.tryGetComponents<TransformComponent, CameraComponent>();
        if (cameraTransform && cameraComponent) {
            cameraPosition = cameraTransform->translation();
            frustum = &cameraComponent->frustum;
        }
    }

    // the chunk grid is scene wide, so it follows one camera rather than each view that draws it
    if (frustum != nullptr) {
        auto terrainView = m_registry.view<TerrainComponent>();
        for (auto entity : terrainView) {
            auto &terrain = terrainView.get<TerrainComponent>(entity);
            if (terrain.generator && terrain.isEnabled && terrain.generator->isInitialized()) {
                terrain.generator->update(cameraPosition, *frustum, frameCounter);
            }
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
        if (cascadedShadowMap != nullptr && shadow.isActive && activeCamera != nullptr) {
            // Update the cascaded shadow map view matrices
            auto cameraComp = activeCamera->entity().tryGetComponent<CameraComponent>();
            if (cameraComp) {
                cascadedShadowMap->setLambda(shadow.lambda);
                cascadedShadowMap->setShadowDistance(shadow.shadowDistance);
                cascadedShadowMap->updateViewMatrix(light, transform, *cameraComp);
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

void Scene::serialize(WriteNode node) const
{
    RAPTURE_PROFILE_FUNCTION();

    node.set(KEY_FORMAT_VERSION, static_cast<uint64_t>(SCENE_FORMAT_VERSION));
    node.set(KEY_NAME, std::string_view(m_config.sceneName));
    node.set(KEY_FRUSTUM_CULLING, m_config.frustumCullingEnabled);

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

SerialDocument Scene::snapshot() const
{
    SerialDocument builder;
    serialize(builder.root());

    // a written document is write mode, so it is parsed back into one that can be read
    return SerialDocument::parse(builder.toText());
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

    return scene;
}

static void s_mapInstancesById(const Instance &parent, std::unordered_map<InstanceId, Instance *> &out)
{
    for (const auto &child : parent.children()) {
        out.emplace(child->id(), child.get());
        s_mapInstancesById(*child, out);
    }
}

// Pre-order, so an instance is under its snapshot parent before its own children come looking for it.
// Everything the snapshot names leaves the map, so what stays behind is what the scene gained since.
static bool s_restoreSubtree(Instance &parent, ReadNode instances, std::unordered_map<InstanceId, Instance *> &live)
{
    for (size_t i = 0; i < instances.size(); i++) {
        ReadNode node = instances.at(i);
        Instance::DocumentHeader header = Instance::readHeader(node);

        Instance *instance = nullptr;
        auto found = live.find(header.id);

        if (found != live.end()) {
            instance = found->second;
            live.erase(found);

            if (instance->parent() != &parent) {
                parent.addChild(instance->parent()->removeChild(instance));
            }
        } else {
            std::unique_ptr<Instance> created = InstanceRegistry::create(header.className, *parent.scene(), header.name);
            if (created == nullptr) {
                RP_CORE_ERROR("no instance class named '{}', needed by '{}'", header.className, header.name);
                return false;
            }

            instance = created.get();
            parent.addChild(std::move(created));
        }

        instance->deserialize(node);

        if (!s_restoreSubtree(*instance, header.children, live)) {
            return false;
        }
    }

    return true;
}

// an instance the snapshot does not hold cannot hold one it does, so its whole subtree goes with it
static void s_destroyInstancesNotInSnapshot(Instance &parent, const std::unordered_map<InstanceId, Instance *> &gained)
{
    for (size_t i = parent.children().size(); i > 0; i--) {
        Instance *child = parent.children()[i - 1].get();

        if (gained.contains(child->id())) {
            parent.removeChild(child);
            continue;
        }

        s_destroyInstancesNotInSnapshot(*child, gained);
    }
}

// TODO: try holding the snapshot as a copy of the instances instead of a document, and restoring by
// assigning those copies over the live ones. Every asset ref would go 1 -> 2 -> 1 and never reach 0,
// so nothing can be evicted and reloaded across a restore. Measure it against reading the document.
bool Scene::restoreFrom(ReadNode node)
{
    RAPTURE_PROFILE_FUNCTION();

    uint32_t formatVersion = static_cast<uint32_t>(node.child(KEY_FORMAT_VERSION).asU64(0));
    if (formatVersion != SCENE_FORMAT_VERSION) {
        RP_CORE_ERROR("cannot read a version {} scene, this build reads version {}", formatVersion, SCENE_FORMAT_VERSION);
        return false;
    }

    m_config.sceneName = std::string(node.child(KEY_NAME).asString(m_config.sceneName));
    m_config.frustumCullingEnabled = node.child(KEY_FRUSTUM_CULLING).asBool(m_config.frustumCullingEnabled);

    std::unordered_map<InstanceId, Instance *> live;
    s_mapInstancesById(*m_root, live);

    if (!s_restoreSubtree(*m_root, node.child(KEY_INSTANCES), live)) {
        return false;
    }

    if (!live.empty()) {
        s_destroyInstancesNotInSnapshot(*m_root, live);
    }

    onHierarchyChanged.fire();
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
