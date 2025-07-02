#pragma once

#include <glm/glm.hpp>
#include <limits>
#include "Scenes/Entities/Entity.h"

namespace Rapture {

enum class LeafType {
    AABB
};

struct BVHNode {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::min());

    EntityID entityID = Entity::null();

    int parentIndex = -1;                                // used for dbvh
    int leftChildIndex = -1;
    int rightChildIndex = -1;
    int height = -1;                                     // used for dbvh

    // A node is a valid leaf if it has no children **and** it is part of the active tree
    // (height >= 0). Free-list / unallocated nodes keep height == -1, so we must
    // exclude them here to avoid treating them as real leaves.
    bool isLeaf() const { return height >= 0 && leftChildIndex == -1; } // used for dbvh
};

}
