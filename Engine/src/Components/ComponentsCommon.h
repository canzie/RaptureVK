#pragma once

#include <glm/glm.hpp>

namespace Rapture {





struct InstanceData {
    glm::mat4 transform;
};

// Light types for the LightComponent
enum class LightType
{
    Point = 0,
    Directional = 1,
    Spot = 2
};

// Light data structure for shader
struct LightData {
    alignas(16) glm::vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    alignas(16) glm::vec4 direction;     // w = range
    alignas(16) glm::vec4 color;         // w = intensity
    alignas(16) glm::vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};


}