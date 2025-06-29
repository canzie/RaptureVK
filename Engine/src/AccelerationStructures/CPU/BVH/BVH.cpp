#include "BVH.h"

#include "Scenes/Scene.h"
#include "Components/Components.h"
#include "Physics/Colliders/ColliderPrimitives.h"

#include "Components/Systems/BoundingBox.h"

#include <algorithm>

namespace Rapture {

BVH::BVH(LeafType leafType)
    : m_leafType(leafType), m_nodes({})
{

}

BVH::~BVH() {
    m_nodes.clear();
}

void BVH::build(std::shared_ptr<Scene> scene) {

    auto& reg = scene->getRegistry();

    // one possible limitation of checking the bounding box and not taking into account the collider bb
    // this can however be solved by using a fake bb by including the collider bb and the mesh to calculate the final bb
    // i dont like that but it could work.
    // otherwise we would need to only use colliders, or allow a boundingboxcomponent to act as collider?
    // but only using colliders is also not possible because we need an aabb here...

    // --> so we will do the large aabb including the collider witouth changing the original bounding box component
    // this means we can use the original bounding box component as the argument for the intersection tests.
    std::vector<BVHNode> primitives;
    auto bbView = reg.view<BoundingBoxComponent, TransformComponent>();

    for (auto entity : bbView) {
        auto [bb, transform] = bbView.get<BoundingBoxComponent, TransformComponent>(entity);
        
        BVHNode node;
        node.entityID = (EntityID)entity;

        bb.updateWorldBoundingBox(transform.transforms.getTransform());

        node.min = bb.worldBoundingBox.getMin();
        node.max = bb.worldBoundingBox.getMax();
        
        if (reg.any_of<RigidBodyComponent>(entity)) {
            auto& collider = reg.get<RigidBodyComponent>(entity);
            glm::vec3 min, max;
            collider.collider->getAABB(min, max);
            node.min = glm::min(node.min, min);
            node.max = glm::max(node.max, max);
        }

        primitives.push_back(node);
    }

    if (primitives.empty()) {
        m_nodes.clear();
        return;
    }

    m_nodes.clear();
    m_nodes.reserve(primitives.size() * 2);

    recursiveBuild(primitives, 0, primitives.size() - 1);
}

std::vector<EntityID> BVH::getIntersectingAABBs(const BoundingBox& worldAABB) const {
    std::vector<EntityID> intersectingEntities;
    if (m_nodes.empty()) {
        return intersectingEntities;
    }
    getIntersectingAABBsRecursive(worldAABB, 0, intersectingEntities);
    return intersectingEntities;
}

void BVH::getIntersectingAABBsRecursive(const BoundingBox& worldAABB, int nodeIndex, std::vector<EntityID>& intersectingEntities) const {
    if (nodeIndex == -1) {
        return;
    }

    const auto& node = m_nodes[nodeIndex];
    BoundingBox nodeAABB(node.min, node.max);

    const auto& world_min = worldAABB.getMin();
    const auto& world_max = worldAABB.getMax();
    const auto& node_min = nodeAABB.getMin();
    const auto& node_max = nodeAABB.getMax();

    bool intersects = (world_max.x > node_min.x &&
        world_min.x < node_max.x &&
        world_max.y > node_min.y &&
        world_min.y < node_max.y &&
        world_max.z > node_min.z &&
        world_min.z < node_max.z);

    if (!intersects) {
        return;
    }

    if (node.leftChildIndex == -1) { // isLeaf
        intersectingEntities.push_back(node.entityID);
        return;
    }

    getIntersectingAABBsRecursive(worldAABB, node.leftChildIndex, intersectingEntities);
    getIntersectingAABBsRecursive(worldAABB, node.rightChildIndex, intersectingEntities);
}

int BVH::recursiveBuild(std::vector<BVHNode>& primitives, size_t start, size_t end) {
    if (start > end) {
        return -1;
    }

    int currentNodeIndex = static_cast<int>(m_nodes.size());
    BVHNode node;
    m_nodes.push_back(node);

    if (start == end) {
        m_nodes[currentNodeIndex] = primitives[start];
        m_nodes[currentNodeIndex].leftChildIndex = -1;
        m_nodes[currentNodeIndex].rightChildIndex = -1;
        return currentNodeIndex;
    }

    glm::vec3 min = primitives[start].min;
    glm::vec3 max = primitives[start].max;
    for (size_t i = start + 1; i <= end; ++i) {
        min = glm::min(min, primitives[i].min);
        max = glm::max(max, primitives[i].max);
    }
    m_nodes[currentNodeIndex].min = min;
    m_nodes[currentNodeIndex].max = max;

    glm::vec3 extent = max - min;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    size_t mid = start + (end - start) / 2;
    std::sort(primitives.begin() + start, primitives.begin() + end + 1,
        [axis](const BVHNode& a, const BVHNode& b) {
            float centerA = (a.min[axis] + a.max[axis]) * 0.5f;
            float centerB = (b.min[axis] + b.max[axis]) * 0.5f;
            return centerA < centerB;
        });

    m_nodes[currentNodeIndex].leftChildIndex = recursiveBuild(primitives, start, mid);
    m_nodes[currentNodeIndex].rightChildIndex = recursiveBuild(primitives, mid + 1, end);
    
    return currentNodeIndex;
}

}