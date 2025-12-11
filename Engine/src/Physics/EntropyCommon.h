#pragma once

#include "Scenes/Entities/Entity.h"
#include "precision.h"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Rapture::Entropy {

struct ContactPoint {
    glm::vec3 worldPointA;
    glm::vec3 worldPointB;
    glm::vec3 normalOnB;
    float penetrationDepth; // negative is no penetration, 0 is just touching, positive is penetration
    float restitution;
    float friction;
};

struct ContactManifold {
    Entity entityA;
    Entity entityB;
    std::vector<ContactPoint> contactPoints;
};

} // namespace Rapture::Entropy