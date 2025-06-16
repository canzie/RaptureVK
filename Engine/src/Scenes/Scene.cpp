#include "Scene.h"
#include "Entities/Entity.h"

#include "Components/Components.h"

namespace Rapture {

    Scene::Scene(const std::string& sceneName) {
        m_config.sceneName = sceneName;
    }

    Scene::~Scene() {
        // EnTT registry will automatically clean up all entities and components
    }

    Entity Scene::createEntity(const std::string& name) {
        // Create entity in the registry
        entt::entity handle = m_Registry.create();
        
        // Create Entity wrapper
        Entity entity(handle, this);
        
        // Add basic name component if you have one
        entity.addComponent<TagComponent>(name);
        
        return entity;
    }

    void Scene::destroyEntity(Entity entity) {
        if (entity.isValid() && entity.getScene() == this) {
            m_Registry.destroy(entity.getHandle());
        }
    }

    void Scene::onUpdate() {

    }

    SceneSettings &Scene::getSettings() {
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
        if (camera.isValid() && camera.hasComponent<CameraComponent>())
            m_config.mainCamera = std::make_shared<Entity>(camera);
    }

    void Scene::setMainCamera(std::shared_ptr<Entity> camera)
    {
        m_config.mainCamera = camera;
    }

    std::weak_ptr<Entity> Scene::getMainCamera() {
        return m_config.mainCamera;
    }

    void Scene::setSkybox(Entity entity) {
        if (entity.isValid() && entity.hasComponent<SkyboxComponent>()) {
             m_config.skybox = std::make_shared<Entity>(entity);
        }
    }

    std::weak_ptr<Entity> Scene::getSkybox() {
        return m_config.skybox;
    }

    SkyboxComponent* Scene::getSkyboxComponent() {
        if (m_config.skybox && m_config.skybox->isValid())
            return m_config.skybox->tryGetComponent<SkyboxComponent>();
        return nullptr;
    }

    void Scene::registerBLAS(Entity& entity) {

        if (!m_tlas) {
            m_tlas = std::make_unique<TLAS>();
        }

        auto [blas, mesh, transform] = entity.tryGetComponents<BLASComponent, MeshComponent, TransformComponent>();
        if (!blas || !mesh || !transform) {
            RP_CORE_ERROR("Scene::registerBLAS: Entity does not have a valid BLAS component");
            return;
        }

        TLASInstance instance;
        instance.blas = blas->blas;
        instance.transform = transform->transformMatrix();
        instance.entityID = entity.getID();
        m_tlas->addInstance(instance);

    }

    void Scene::registerBLAS(std::shared_ptr<Entity> entity) {
        if (!m_tlas) {
            m_tlas = std::make_unique<TLAS>();
        }

        auto [blas, mesh, transform] = entity->tryGetComponents<BLASComponent, MeshComponent, TransformComponent>();
        if (!blas || !mesh || !transform) {
            RP_CORE_ERROR("Scene::registerBLAS: Entity does not have a valid BLAS component");
            return;
        }

        TLASInstance instance;
        instance.blas = blas->blas;
        instance.transform = transform->transformMatrix();
        instance.entityID = entity->getID();
        m_tlas->addInstance(instance);
    
    }
    void Scene::buildTLAS() {
        if (!m_tlas) {
            RP_CORE_ERROR("Scene::buildTLAS: TLAS is not initialized");
            return;
        }

        m_tlas->build();

    }
}