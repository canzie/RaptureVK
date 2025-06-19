#include "Scene.h"
#include "Entities/Entity.h"

#include "Components/Components.h"

#include "AssetManager/AssetManager.h"
#include "Meshes/MeshPrimitives.h"

#include <memory>



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

    Entity Scene::createCube(const std::string &name) {
        // Create entity in the registry
        entt::entity handle = m_Registry.create();
        
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

    Entity Scene::createSphere(const std::string &name) {
        // Create entity in the registry
        entt::entity handle = m_Registry.create();
        
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

    void Scene::destroyEntity(Entity entity) {
        if (entity.isValid() && entity.getScene() == this) {
            m_Registry.destroy(entity.getHandle());
        }
    }

    void Scene::onUpdate(uint32_t dt) {
        updateTLAS();
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

    // TODO: update this so we update the transform directly instead of sotring the change and letting the tlas go over it again
    void Scene::updateTLAS() {
        auto view = m_Registry.view<TransformComponent, BLASComponent>();

        std::vector<std::pair<uint32_t, glm::mat4>> instanceUpdates;
        auto& instances = m_tlas->getInstances();
        int instanceIndex = 0;

        for (auto& instance : instances) {
            auto entity = Entity(instance.entityID, this);
            
            if (entity.isValid()) {
                auto [transform] = entity.tryGetComponents<TransformComponent>();
                if (transform && transform->hasChanged(0))  { // we just lie about the haschanged index, this SHOULD just force a recalc which is fine, i SHOULD not break logic.
                    instanceUpdates.push_back({instanceIndex, transform->transformMatrix()});
                }
            }
        instanceIndex++;

        }
        m_tlas->updateInstances(instanceUpdates);
    }
}