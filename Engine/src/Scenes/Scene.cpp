#include "Scene.h"
#include "Entities/Entity.h"

#include "Components/Components.h"
#include "Components/TerrainComponent.h"

#include "AssetManager/AssetManager.h"
#include "Meshes/MeshPrimitives.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "WindowContext/Application.h"

#include <memory>

namespace Rapture {

#define ENVIRONMENT_ENTITY_TAG "Environment"

Scene::Scene(const std::string &sceneName)
{
    m_config.sceneName = sceneName;
}

Scene::~Scene()
{
    // EnTT registry will automatically clean up all entities and components
}

Entity Scene::createEntity(const std::string &name)
{
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
    // Create entity in the registry
    entt::entity handle = m_registry.create();

    // Create Entity wrapper
    Entity entity(handle, this);

    // Add basic name component if you have one
    entity.addComponent<TagComponent>(name);

    entity.addComponent<TransformComponent>(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));

    // Add a cube mesh
    auto cubeMesh = std::make_shared<Mesh>(Primitives::CreateCube());
    entity.addComponent<MeshComponent>(cubeMesh);

    entity.addComponent<BoundingBoxComponent>(glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f));

    // Add a material
    auto material = AssetManager::importDefaultAsset<MaterialInstance>(AssetType::Material).first;
    if (material) {
        entity.addComponent<MaterialComponent>(material);
    }

    return entity;
}

Entity Scene::createSphere(const std::string &name)
{
    // Create entity in the registry
    entt::entity handle = m_registry.create();

    // Create Entity wrapper
    Entity entity(handle, this);

    // Add basic name component if you have one
    entity.addComponent<TagComponent>(name);

    entity.addComponent<TransformComponent>(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));

    // Add a cube mesh
    auto sphereMesh = std::make_shared<Mesh>(Primitives::CreateSphere(1.0f, 32));
    entity.addComponent<MeshComponent>(sphereMesh);

    entity.addComponent<BoundingBoxComponent>(glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f));

    // Add a material
    auto material = AssetManager::importDefaultAsset<MaterialInstance>(AssetType::Material).first;
    if (material) {
        entity.addComponent<MaterialComponent>(material);
    }

    return entity;
}

void Scene::destroyEntity(Entity entity)
{
    if (entity.isValid() && entity.getScene() == this) {
        m_registry.destroy(entity.getHandle());
    }
}

void Scene::onUpdate(float dt)
{
    (void)dt;
    static uint32_t frameCounter = 0;

    // Get current frame dimensions for camera updates
    auto &app = Application::getInstance();
    auto swapChain = app.getVulkanContext().getSwapChain();
    float width = static_cast<float>(swapChain->getExtent().width);
    float height = static_cast<float>(swapChain->getExtent().height);
    uint32_t frameCount = swapChain->getImageCount();

    // Update mesh data buffers
    auto meshView = m_registry.view<TransformComponent, MeshComponent, MaterialComponent, TagComponent>();
    for (auto entity : meshView) {
        auto [transform, mesh, material, tag] =
            meshView.get<TransformComponent, MeshComponent, MaterialComponent, TagComponent>(entity);

        // nothing changed
        if (transform.hasChanged()) {
            // TODO: This will break the last frame if the transform is not continuesly changed
            // for example frame 1 and 2 will have isdirty to true, then frame 3 will have isdirty to false
            // this works here but the other updates like for the lights will break
            // after this we should be clean
            if (transform.dirtyFrames == frameCount) {
                *transform.isDirty = false;
                transform.dirtyFrames = 0;
            } else {
                transform.dirtyFrames++;
            }

            Entity ent = Entity(entity, this);
            // publish the event
            AssetEvents::onMeshTransformChanged().publish(ent.getID());
        }

        // should be shot for this but feels kindof dumb to do another loop just for the materials
        // + when there are no more pending texutres, it should be 0 cost since we just check if the vector is empty then return.
        material.material->updatePendingTextures();

        uint32_t vertexFlags = mesh.mesh->getVertexBuffer()->getBufferLayout().getFlags();
        uint32_t materialFlags = material.material->getMaterialFlags();
        uint32_t flags = vertexFlags | materialFlags;

        mesh.meshDataBuffer->update(transform, flags, frameCounter);
    }

    // Update camera data buffer
    auto cameraView = m_registry.view<TransformComponent, CameraComponent>();
    for (auto entity : cameraView) {
        auto [transform, camera] = cameraView.get<TransformComponent, CameraComponent>(entity);

        // Update camera aspect ratio based on current swapchain extent
        float aspectRatio = width / height;
        if (camera.aspectRatio != aspectRatio) {
            camera.updateProjectionMatrix(camera.fov, aspectRatio, camera.nearPlane, camera.farPlane);
        }
        camera.cameraDataBuffer->update(camera, frameCounter);
    }

    // Update light data buffers
    auto lightView = m_registry.view<LightComponent, TransformComponent>();
    for (auto entity : lightView) {
        auto [light, transform] = lightView.get<LightComponent, TransformComponent>(entity);

        if (light.hasChanged(frameCounter) || transform.hasChanged() || light.type == LightType::Directional ||
            light.type == LightType::Spot) {
            light.lightDataBuffer->update(transform, light, static_cast<uint32_t>(entity));
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
        if (terrain.isEnabled && terrain.generator.isInitialized()) {
            terrain.generator.update(cameraPosition, *frustum);
        }
    }

    // Update regular shadow maps
    auto shadowView = m_registry.view<LightComponent, TransformComponent, ShadowComponent>();
    for (auto entity : shadowView) {
        auto [light, transform, shadow] = shadowView.get<LightComponent, TransformComponent, ShadowComponent>(entity);

        if (shadow.shadowMap && shadow.isActive &&
            (light.hasChanged(frameCounter) || transform.hasChanged() || light.type == LightType::Spot)) {

            // Update the shadow map view matrix
            shadow.shadowMap->updateViewMatrix(light, transform, cameraPosition);

            auto shadowDataBuffer = shadow.shadowMap->getShadowDataBuffer();
            // Update the shadow data buffer if it exists
            if (shadowDataBuffer) {
                shadowDataBuffer->update(light, shadow, static_cast<uint32_t>(entity));
            }
        }
    }

    // Update cascaded shadow maps
    auto cascadedShadowView = m_registry.view<LightComponent, TransformComponent, CascadedShadowComponent>();
    for (auto entity : cascadedShadowView) {
        auto [light, transform, shadow] =
            cascadedShadowView.get<LightComponent, TransformComponent, CascadedShadowComponent>(entity);

        if (shadow.cascadedShadowMap && shadow.isActive) {
            // Update the cascaded shadow map view matrices
            Entity mainCamera = getMainCamera();
            if (mainCamera.isValid()) {
                auto cameraComp = mainCamera.tryGetComponent<CameraComponent>();
                if (cameraComp) {
                    shadow.cascadedShadowMap->updateViewMatrix(light, transform, *cameraComp);

                    auto shadowDataBuffer = shadow.cascadedShadowMap->getShadowDataBuffer();
                    // Update the shadow data buffer if it exists
                    if (shadowDataBuffer) {
                        shadowDataBuffer->update(light, shadow, static_cast<uint32_t>(entity));
                    }
                }
            }
        }
    }

    frameCounter = (frameCounter + 1) % frameCount;

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

Entity Scene::createEnvironmentEntity()
{
    // Check if environment entity already exists
    auto view = m_registry.view<TagComponent>();
    for (auto entity : view) {
        if (view.get<TagComponent>(entity).tag == ENVIRONMENT_ENTITY_TAG) {
            return Entity(entity, this);
        }
    }

    // Create new environment entity
    return createEntity(ENVIRONMENT_ENTITY_TAG);
}

Entity Scene::getEnvironmentEntity() const
{
    // Query for environment entity
    auto view = m_registry.view<TagComponent>();
    for (auto entity : view) {
        if (view.get<TagComponent>(entity).tag == ENVIRONMENT_ENTITY_TAG) {
            return Entity(entity, const_cast<Scene *>(this));
        }
    }
    return Entity::null();
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
    instance.blas = blas->blas;
    instance.transform = transform->transformMatrix();
    instance.entityID = entity.getID();
    m_tlas->addInstance(instance);
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
            auto [transform] = entity.tryGetComponents<TransformComponent>();
            if (transform && transform->hasChanged()) { // we just lie about the haschanged index, this SHOULD just force a recalc
                                                        // which is fine, i SHOULD not break logic.
                instanceUpdates.push_back({instanceIndex, transform->transformMatrix()});
            }
        }
        instanceIndex++;
    }

    m_tlas->updateInstances(instanceUpdates);
}
} // namespace Rapture