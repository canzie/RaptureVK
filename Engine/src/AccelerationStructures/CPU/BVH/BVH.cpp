#include "BVH.h"

#include "Scenes/Scene.h"
#include "Components/Components.h"
#include "Physics/Colliders/ColliderPrimitives.h"
#include "Physics/EntropyComponents.h"
#include "Components/Systems/BoundingBox.h"

#include <algorithm>
#include <unordered_set>

namespace Rapture {

BVH::BVH(LeafType leafType)
    : m_leafType(leafType), m_nodes({})
{

}

BVH::~BVH() {
    m_nodes.clear();
}

void BVH::build(std::shared_ptr<Scene> scene) {

    m_nodes.clear();

    auto& reg = scene->getRegistry();

    std::vector<BVHNode> primitives;
    auto bbView = reg.view<BoundingBoxComponent, Entropy::RigidBodyComponent, TransformComponent, MeshComponent>();

    for (auto entity : bbView) {
        auto [bb, rb, transform, mesh] = bbView.get<BoundingBoxComponent, Entropy::RigidBodyComponent, TransformComponent, MeshComponent>(entity);
        
        // Skip dynamic objects
        if (!mesh.isStatic) {
            continue;
        }



        glm::vec3 minLocal, maxLocal;
        rb.collider->getAABB(minLocal, maxLocal);

        BoundingBox aabb = BoundingBox(minLocal, maxLocal);

        BoundingBox localAABB = aabb.transform(rb.collider->localTransform) + bb.localBoundingBox;


        BoundingBox worldAABB = localAABB.transform(transform.transformMatrix());

        BVHNode node;
        node.entityID = (EntityID)entity;
        node.min = worldAABB.getMin();
        node.max = worldAABB.getMax();
        

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
    // Early-out if the BVH is empty.
    if (m_nodes.empty()) {
        return {};
    }

    // Collect intersecting IDs – we might visit the same entity multiple times
    // depending on how the query is performed (e.g. due to duplicate leaves or
    // overlapping internal nodes). Use a set to guarantee uniqueness.
    std::vector<EntityID> collected;
    collected.reserve(8);
    getIntersectingAABBsRecursive(worldAABB, 0, collected);

    std::unordered_set<EntityID> uniqueIds;
    uniqueIds.reserve(collected.size());

    for (EntityID id : collected) {
        // Filter out invalid / null entities that may live on internal or free nodes.
        if (id != static_cast<EntityID>(Entity::null())) {
            uniqueIds.insert(id);
        }
    }

    return std::vector<EntityID>(uniqueIds.begin(), uniqueIds.end());
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
        m_nodes[currentNodeIndex].height = 0; // mark as valid leaf
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

    int leftIdx = recursiveBuild(primitives, start, mid);
    int rightIdx = recursiveBuild(primitives, mid + 1, end);

    m_nodes[currentNodeIndex].leftChildIndex = leftIdx;
    m_nodes[currentNodeIndex].rightChildIndex = rightIdx;

    // Height is 1 + max child height (children heights are >=0)
    int leftHeight  = (leftIdx  != -1) ? m_nodes[leftIdx].height  : -1;
    int rightHeight = (rightIdx != -1) ? m_nodes[rightIdx].height : -1;
    m_nodes[currentNodeIndex].height = 1 + std::max(leftHeight, rightHeight);

    return currentNodeIndex;
}

}