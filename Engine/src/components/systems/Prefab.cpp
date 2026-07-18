#include "Prefab.h"

#include "asset_manager/AssetManager.h"
#include "components/Components.h"
#include "components/HierarchyComponent.h"
#include "logging/Log.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"

#include <vector>

namespace Rapture {

Entity Prefab::instantiate(AssetRef prefabRef, Scene *scene, const glm::mat4 &rootTransform)
{
    if (scene == nullptr) {
        RP_CORE_ERROR("Cannot instantiate prefab into a null scene");
        return Entity::null();
    }

    if (!prefabRef) {
        RP_CORE_ERROR("Cannot instantiate an invalid prefab ref");
        return Entity::null();
    }

    Prefab *prefab = prefabRef.get()->getUnderlyingAsset<Prefab>();
    if (prefab == nullptr) {
        RP_CORE_ERROR("Prefab ref does not hold a prefab asset");
        return Entity::null();
    }

    Entity root = scene->createEntity(prefab->m_name);
    root.addComponent<TransformComponent>(rootTransform);
    root.addComponent<PrefabComponent>(prefabRef);

    const std::vector<Node> &nodes = prefab->m_nodes;
    if (nodes.empty()) {
        RP_CORE_WARN("Instantiating prefab '{}' with no nodes", prefab->m_name);
    }

    std::vector<Entity> entities(nodes.size());
    std::vector<glm::mat4> worldTransforms(nodes.size());

    // Nodes are pre-order, so a parent is processed before its children. Transforms are flattened
    // to world here because transform propagation does not exist yet.
    for (size_t i = 0; i < nodes.size(); i++) {
        const Node &node = nodes[i];

        glm::mat4 parentWorld = (node.parent >= 0) ? worldTransforms[node.parent] : rootTransform;
        worldTransforms[i] = parentWorld * node.localTransform;

        Entity entity = scene->createEntity(node.name);
        entity.addComponent<TransformComponent>(worldTransforms[i]);
        entities[i] = entity;

        Entity parentEntity = (node.parent >= 0) ? entities[node.parent] : root;
        HierarchyComponent::setParent(entity, parentEntity);

        if (!node.hasMesh()) {
            continue;
        }

        AssetRef meshRef = AssetManager::getAsset(node.mesh);
        if (!meshRef) {
            RP_CORE_ERROR("Prefab '{}' node '{}' references missing mesh {}", prefab->m_name, node.name, node.mesh);
            continue;
        }

        auto &meshComp = entity.addComponent<MeshComponent>(meshRef);
        if (meshComp.mesh) {
            if (node.hasBoundingBox()) {
                entity.addComponent<BoundingBoxComponent>(node.boundingBoxMin, node.boundingBoxMax);
            }
            entity.addComponent<BLASComponent>(meshComp.mesh);
            scene->registerBLAS(entity);
        }

        if (node.material != INVALID_ASSET_HANDLE) {
            AssetRef matRef = AssetManager::getAsset(node.material);
            if (matRef) {
                entity.addComponent<MaterialComponent>(matRef);
            } else {
                RP_CORE_WARN("Prefab '{}' node '{}' references missing material {}", prefab->m_name, node.name, node.material);
            }
        }
    }

    return root;
}

} // namespace Rapture
