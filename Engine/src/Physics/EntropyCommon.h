#pragma once

#include "precision.h"
#include "Scenes/Entities/Entity.h"

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <array>
#include <vector>
#include <memory>

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




}