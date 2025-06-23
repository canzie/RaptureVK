#include "BVH_SAH.h"

#include "Scenes/Scene.h"
#include "Components/Components.h"
#include "Physics/Colliders/ColliderPrimitives.h"
#include "Components/Systems/BoundingBox.h"

#include <algorithm>
#include <iostream>

namespace Rapture {

inline float calculateSurfaceArea(const BVHNode& node) {
    glm::vec3 extent = node.max - node.min;
    return 2.0f * (extent.x * extent.y + extent.x * extent.z + extent.y * extent.z);
}

BVH_SAH::BVH_SAH(LeafType leafType)
    : m_leafType(leafType) {}

BVH_SAH::~BVH_SAH() {
    m_nodes.clear();
}

void BVH_SAH::build(std::shared_ptr<Scene> scene) {
    auto& reg = scene->getRegistry();
    std::vector<BVHNode> primitives;
    auto bbView = reg.view<BoundingBoxComponent>();

    for (auto entity : bbView) {
        auto& bb = bbView.get<BoundingBoxComponent>(entity);
        BVHNode node;
        node.entityID = (EntityID)entity;
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

int BVH_SAH::recursiveBuild(std::vector<BVHNode>& primitives, size_t start, size_t end) {
    if (start > end) {
        return -1;
    }

    size_t numPrimitives = end - start + 1;
    int currentNodeIndex = static_cast<int>(m_nodes.size());
    BVHNode node;
    m_nodes.push_back(node);

    BVHNode& currentNode = m_nodes[currentNodeIndex];
    currentNode.min = primitives[start].min;
    currentNode.max = primitives[start].max;
    for (size_t i = start + 1; i <= end; ++i) {
        currentNode.min = glm::min(currentNode.min, primitives[i].min);
        currentNode.max = glm::max(currentNode.max, primitives[i].max);
    }

    float parentSurfaceArea = calculateSurfaceArea(currentNode);
    float bestCost = std::numeric_limits<float>::max();
    int bestAxis = -1;
    size_t bestSplitIndex = -1;

    for (int axis = 0; axis < 3; ++axis) {
        std::sort(primitives.begin() + start, primitives.begin() + end + 1,
            [axis](const BVHNode& a, const BVHNode& b) {
                return (a.min[axis] + a.max[axis]) < (b.min[axis] + b.max[axis]);
            });

        std::vector<float> leftAreas(numPrimitives);
        BVHNode leftBox;
        leftBox.min = primitives[start].min;
        leftBox.max = primitives[start].max;
        leftAreas[0] = calculateSurfaceArea(leftBox);
        for (size_t i = 1; i < numPrimitives; ++i) {
            leftBox.min = glm::min(leftBox.min, primitives[start + i].min);
            leftBox.max = glm::max(leftBox.max, primitives[start + i].max);
            leftAreas[i] = calculateSurfaceArea(leftBox);
        }

        BVHNode rightBox;
        rightBox.min = primitives[end].min;
        rightBox.max = primitives[end].max;
        float rightArea = calculateSurfaceArea(rightBox);

        for (size_t i = numPrimitives - 2; i >= 0; --i) {
            float cost = 0.125f + (leftAreas[i] * static_cast<float>(i + 1) + rightArea * static_cast<float>(numPrimitives - 1 - i)) / parentSurfaceArea;

            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestSplitIndex = start + i;
            }

            rightBox.min = glm::min(rightBox.min, primitives[start + i].min);
            rightBox.max = glm::max(rightBox.max, primitives[start + i].max);
            rightArea = calculateSurfaceArea(rightBox);
        }
    }

    float leafCost = static_cast<float>(numPrimitives);
    if (bestCost >= leafCost || numPrimitives <= 1) {
        // Create leaf
        currentNode.entityID = primitives[start].entityID;
        currentNode.leftChildIndex = -1;
        currentNode.rightChildIndex = -1;
        return currentNodeIndex;
    }
    
    std::sort(primitives.begin() + start, primitives.begin() + end + 1,
        [bestAxis](const BVHNode& a, const BVHNode& b) {
            return (a.min[bestAxis] + a.max[bestAxis]) < (b.min[bestAxis] + b.max[bestAxis]);
        });
    
    currentNode.leftChildIndex = recursiveBuild(primitives, start, bestSplitIndex);
    currentNode.rightChildIndex = recursiveBuild(primitives, bestSplitIndex + 1, end);
    
    return currentNodeIndex;
}

std::vector<EntityID> BVH_SAH::getIntersectingAABBs(const BoundingBox& worldAABB) const {
    std::vector<EntityID> intersectingEntities;
    if (m_nodes.empty()) {
        return intersectingEntities;
    }
    getIntersectingAABBsRecursive(worldAABB, 0, intersectingEntities);
    return intersectingEntities;
}

void BVH_SAH::getIntersectingAABBsRecursive(const BoundingBox& worldAABB, int nodeIndex, std::vector<EntityID>& intersectingEntities) const {
    if (nodeIndex == -1) {
        return;
    }

    const auto& node = m_nodes[nodeIndex];
    const auto& world_min = worldAABB.getMin();
    const auto& world_max = worldAABB.getMax();
    const auto& node_min = node.min;
    const auto& node_max = node.max;

    bool intersects = (world_max.x > node_min.x &&
        world_min.x < node_max.x &&
        world_max.y > node_min.y &&
        world_min.y < node_max.y &&
        world_max.z > node_min.z &&
        world_min.z < node_max.z);

    if (!intersects) {
        return;
    }

    if (node.leftChildIndex == -1) { 
        intersectingEntities.push_back(node.entityID);
        return;
    }

    getIntersectingAABBsRecursive(worldAABB, node.leftChildIndex, intersectingEntities);
    getIntersectingAABBsRecursive(worldAABB, node.rightChildIndex, intersectingEntities);
}

} 