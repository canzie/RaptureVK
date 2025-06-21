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

    bool isLeaf() const { return leftChildIndex == -1; } // used for dbvh
};

}
