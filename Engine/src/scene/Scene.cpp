#include "Scene.h"

#include "scene/components/Components.h"
#include "scene/components/TerrainComponent.h"
#include "scene/systems/Transforms.h"
#include "scene/instances/controllers/Controller.h"
#include "scene/instances/Camera3D.h"
#include "scene/instances/DirectionalLight3D.h"
#include "scene/instances/Environment.h"
#include "scene/instances/Node3D.h"
#include "scene/instances/PhysicsBody3D.h"
#include "scene/instances/StaticMesh3D.h"
#include "scene/render_data/SceneRenderData.h"
#include "renderer/shadows/CascadedShadowMapping.h"
#include "renderer/shadows/ShadowMapping.h"
#include "scene/instances/SceneObject.h"
#include "scene/instances/InstanceRegistry.h"
#include "scene/SceneLoadContext.h"

#include "assets/asset_manager/AssetManager.h"
#include "assets/asset_manager/ReservedAssets.h"
#include "core/utils/TracyProfiler.h"
#include "physics/PhysicsSystem.h"
#include "scene/EntityCommon.h"
#include "core/serialization/SerialDocument.h"
#include "app/Application.h"

#include <memory>
#include <unordered_set>
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

    m_root = std::make_unique<SceneObject>(*this, "Root");
    m_root->add<Environment>("Environment");

    m_physics = std::make_unique<PhysicsSystem>();
}

Scene::~Scene() = default;

ecs::EntityAccessor Scene::createEntity(const std::string &name)
{
    if (locked) {
        RP_CORE_WARN("Cannot create entity in locked scene '{}'", getSceneName());
        return ecs::EntityAccessor();
    }

    ecs::EntityAccessor entity(m_registry.create(), &m_registry);
    entity.add<TagComponent>(name);

    return entity;
}

ecs::EntityAccessor Scene::createCube(const std::string &name, Mobility mobility)
{
    if (locked) {
        RP_CORE_WARN("Cannot create entity in locked scene '{}'", getSceneName());
        return ecs::EntityAccessor();
    }

    ecs::EntityAccessor entity(m_registry.create(), &m_registry);
    entity.add<TagComponent>(name);
    entity.add<TransformComponent>();
    entity.add<StaticMeshComponent>(AssetManager::getAsset(RE_PRIMITIVE_CUBE_MESH), mobility);

    auto materialRef = AssetManager::importDefaultAsset(ASSET_MATERIAL_INSTANCE);
    if (materialRef) {
        entity.add<MaterialComponent>(materialRef);
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

ecs::EntityAccessor Scene::createSphere(const std::string &name, Mobility mobility)
{
    if (locked) {
        RP_CORE_WARN("Cannot create entity in locked scene '{}'", getSceneName());
        return ecs::EntityAccessor();
    }

    ecs::EntityAccessor entity(m_registry.create(), &m_registry);
    entity.add<TagComponent>(name);
    entity.add<TransformComponent>();
    entity.add<StaticMeshComponent>(AssetManager::getAsset(RE_PRIMITIVE_SPHERE_MESH), mobility);

    auto materialRef = AssetManager::importDefaultAsset(ASSET_MATERIAL_INSTANCE);
    if (materialRef) {
        entity.add<MaterialComponent>(materialRef);
    }

    return entity;
}

void Scene::destroyEntity(ecs::Entity entity)
{
    if (!m_registry.isValid(entity)) {
        return;
    }

    if (m_registry.has<RayTracedComponent>(entity)) {
        unregisterBLAS(entity);
    }

    m_registry.destroy(entity);
}

void Scene::stepPhysics(float dt)
{
    if (m_physics == nullptr) {
        return;
    }

    m_physics->onUpdate(dt);
    syncSimulatedTransforms();
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
        for (auto [entity, material] : m_registry.read<MaterialComponent>().with<TransformComponent, StaticMeshComponent>()) {
            if (!material.material) {
                continue;
            }
            material.material->updatePendingTextures();
        }
    }

    glm::vec3 cameraPosition = glm::vec3(0.0f);
    Frustum *frustum = nullptr;

    if (m_activeController != nullptr) {
        m_activeController->updateViewCamera();
    }

    Camera3D *activeCamera = m_activeController != nullptr ? m_activeController->viewCamera() : nullptr;
    if (activeCamera != nullptr) {
        ecs::Entity camera = activeCamera->entity();
        if (m_registry.hasAll<TransformComponent, CameraComponent>(camera)) {
            cameraPosition = transform::translation(activeCamera->worldTransform());
            // the frustum holds GPU upload state, so touching it is bookkeeping rather than a camera change
            frustum = &m_registry.write<CameraComponent>(camera, 0)->frustum;
        }
    }

    // the chunk grid is scene wide, so it follows one camera rather than each view that draws it
    if (frustum != nullptr) {
        for (auto [entity, terrain] : m_registry.mutableView<TerrainComponent>()) {
            if (terrain.generator && terrain.isEnabled && terrain.generator->isInitialized()) {
                terrain.generator->update(cameraPosition, *frustum, frameCounter);
            }
        }
    }

    if (m_environment != nullptr) {
        m_environment->update();
    }

    updateShadowViews(cameraPosition, activeCamera);

    m_renderData->onUpdate(frameCounter);

    updateTLAS();
}

void Scene::updateShadowViews(const glm::vec3 &cameraPosition, Camera3D *activeCamera)
{
    ecs::Journal &journal = m_registry.getJournal();
    ecs::Batch transforms = journal.readSince(CHANNEL_TRANSFORM_WORLD, m_shadowTransformBookmark);
    ecs::Batch lights = journal.readSince(CHANNEL_LIGHT_PARAMS, m_shadowLightBookmark);

    bool rebuildAll = transforms.needsRebuild() || lights.needsRebuild();

    std::unordered_set<ecs::Entity> changed;
    if (!rebuildAll) {
        changed.insert(transforms.begin(), transforms.end());
        changed.insert(lights.begin(), lights.end());
    }

    for (auto [entity, shadow] : m_registry.read<ShadowComponent>().with<TransformComponent>()) {
        if (!shadow.isActive || (!rebuildAll && changed.count(entity) == 0)) {
            continue;
        }

        ecs::EntityAccessor accessor(entity, &m_registry);
        if (Light_tryReadLight(accessor) == nullptr) {
            continue;
        }

        ShadowMap *shadowMap = m_renderData->getShadowMap(entity);
        if (shadowMap == nullptr) {
            continue;
        }

        shadowMap->updateViewMatrix(accessor, m_registry.read<TransformComponent>(entity), cameraPosition);

        // the shadow row mirrors the map's matrices, which no component write announces
        journal.record(entity, ecs::ChannelBit(CHANNEL_SHADOW_SETTINGS));
    }

    if (activeCamera == nullptr) {
        return;
    }

    const CameraComponent *camera = m_registry.tryRead<CameraComponent>(activeCamera->entity());
    if (camera == nullptr) {
        return;
    }

    // a cascade split follows the camera, so it is rebuilt every frame rather than on a change
    for (auto [entity, light, transform, shadow] :
         m_registry.read<DirectionalLightComponent, TransformComponent, CascadedShadowComponent>()) {
        CascadedShadowMap *cascadedShadowMap = m_renderData->getCascadedShadowMap(entity);
        if (cascadedShadowMap == nullptr || !shadow.isActive) {
            continue;
        }

        cascadedShadowMap->setLambda(shadow.lambda);
        cascadedShadowMap->setShadowDistance(shadow.shadowDistance);
        cascadedShadowMap->updateViewMatrix(light, transform, *camera);

        journal.record(entity, ecs::ChannelBit(CHANNEL_SHADOW_SETTINGS));
    }
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

void Scene::syncSimulatedTransforms()
{
    m_physics->getSimulatedStates(m_simulatedStates);

    for (const physics::BodyState &state : m_simulatedStates) {
        PhysicsBody3D *body = static_cast<PhysicsBody3D *>(state.owner);
        if (body == nullptr) {
            continue;
        }

        body->applySimulatedTransform(state.position, state.rotation);
    }
}

uint32_t Scene::registerTick(Instance *instance, TickPhase phase)
{
    return m_ticking[phase].insert(instance);
}

void Scene::runTickPhase(TickPhase phase, float dt)
{
    RAPTURE_PROFILE_SCOPE("Scene::runTickPhase");

    m_ticking[phase].forEach([dt](uint32_t slot, Instance *instance) {
        (void)slot;
        instance->onUpdate(dt);
    });
}

void Scene::unregisterTick(uint32_t slot, TickPhase phase)
{
    m_ticking[phase].remove(slot);
}

SceneObject *Scene::instanceFor(ecs::Entity entity) const
{
    const InstanceComponent *ref = m_registry.tryRead<InstanceComponent>(entity);
    return ref != nullptr ? ref->instance : nullptr;
}

void Scene::destroyInstance(SceneObject *instance)
{
    if (instance == nullptr || instance == m_root.get()) {
        return;
    }

    SceneObject *parent = instance->parent();
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

    // the instances are the ones the document was written from, so they keep the ids it gave them
    SceneLoadContext context(false);

    ReadNode instances = node.child(KEY_INSTANCES);
    for (size_t i = 0; i < instances.size(); i++) {
        if (SceneObject::loadSubtree(*scene->m_root, instances.at(i), context) == nullptr) {
            RP_CORE_ERROR("scene '{}' could not be read", scene->m_config.sceneName);
            return nullptr;
        }
    }

    context.finish();

    return scene;
}

static void s_mapInstancesById(const SceneObject &parent, std::unordered_map<InstanceId, SceneObject *> &out)
{
    for (const auto &child : parent.children()) {
        out.emplace(child->id(), child.get());
        s_mapInstancesById(*child, out);
    }
}

// Pre-order, so an instance is under its snapshot parent before its own children come looking for it.
// Everything the snapshot names leaves the map, so what stays behind is what the scene gained since.
static bool s_restoreSubtree(SceneObject &parent, ReadNode instances, std::unordered_map<InstanceId, SceneObject *> &live,
                             SceneLoadContext &context)
{
    for (size_t i = 0; i < instances.size(); i++) {
        ReadNode node = instances.at(i);
        SceneObject::DocumentHeader header = SceneObject::readHeader(node);

        SceneObject *instance = nullptr;
        auto found = live.find(header.id);

        if (found != live.end()) {
            instance = found->second;
            live.erase(found);

            if (instance->parent() != &parent) {
                parent.addChild(instance->parent()->removeChild(instance));
            }
        } else {
            std::unique_ptr<SceneObject> created = InstanceRegistry::createObject(header.className, *parent.scene(), header.name);
            if (created == nullptr) {
                RP_CORE_ERROR("no instance class named '{}', needed by '{}'", header.className, header.name);
                return false;
            }

            instance = created.get();
            parent.addChild(std::move(created));
        }

        instance->deserialize(node);
        context.addInstance(header.id, instance);

        if (!s_restoreSubtree(*instance, header.children, live, context)) {
            return false;
        }
    }

    return true;
}

// an instance the snapshot does not hold cannot hold one it does, so its whole subtree goes with it
static void s_destroyInstancesNotInSnapshot(SceneObject &parent, const std::unordered_map<InstanceId, SceneObject *> &gained)
{
    for (size_t i = parent.children().size(); i > 0; i--) {
        SceneObject *child = parent.children()[i - 1].get();

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

    // instances dropped below take their GPU resources with them, and frames already submitted may
    // still be reading those
    Application::getInstance().getVulkanContext().waitIdle();

    std::unordered_map<InstanceId, SceneObject *> live;
    s_mapInstancesById(*m_root, live);

    // an instance the snapshot holds keeps the id it was written under, restored or reused alike
    SceneLoadContext context(false);

    if (!s_restoreSubtree(*m_root, node.child(KEY_INSTANCES), live, context)) {
        return false;
    }

    if (!live.empty()) {
        s_destroyInstancesNotInSnapshot(*m_root, live);
    }

    // after the drop, so a reference the snapshot no longer holds is not resolved and then emptied
    context.finish();

    onHierarchyChanged.fire();
    return true;
}

void Scene::registerBLAS(ecs::Entity entity)
{
    if (!m_tlas) {
        m_tlas = std::make_unique<TLAS>();
    }

    const StaticMeshComponent *mesh = m_registry.tryRead<StaticMeshComponent>(entity);
    const TransformComponent *transform = m_registry.tryRead<TransformComponent>(entity);
    if (!m_registry.has<RayTracedComponent>(entity) || mesh == nullptr || transform == nullptr || !mesh->mesh) {
        RP_CORE_ERROR("Entity cannot be ray traced");
        return;
    }

    BLAS *blas = mesh->mesh->geometry().getBLAS();
    if (blas == nullptr) {
        RP_CORE_ERROR("Entity's mesh has no acceleration structure");
        return;
    }

    TLASInstance instance;
    instance.blas = blas;
    instance.transform = transform->world;
    instance.entityId = entity;
    m_tlas->addInstance(instance);
}

void Scene::unregisterBLAS(ecs::Entity entity)
{
    if (m_tlas == nullptr) {
        return;
    }

    m_tlas->removeInstance(entity);
}

void Scene::updateTLAS()
{
    if (!m_tlas) {
        return;
    }

    if (m_tlas->needsRebuild()) {
        if (m_tlas->getInstanceCount() > 0) {
            m_tlas->build();
        }
        return;
    }

    ecs::Batch transforms = m_registry.getJournal().readSince(CHANNEL_TRANSFORM_WORLD, m_tlasTransformBookmark);

    if (transforms.needsRebuild()) {
        for (const TLASInstance &instance : m_tlas->getInstances()) {
            const TransformComponent *transform = m_registry.tryRead<TransformComponent>(instance.entityId);
            if (transform != nullptr) {
                m_tlas->setInstanceTransform(instance.entityId, transform->world);
            }
        }
    } else {
        for (ecs::Entity entity : transforms) {
            const TransformComponent *transform = m_registry.tryRead<TransformComponent>(entity);
            if (transform != nullptr) {
                m_tlas->setInstanceTransform(entity, transform->world);
            }
        }
    }

    m_tlas->flushInstanceUpdates();
}
} // namespace Rapture
