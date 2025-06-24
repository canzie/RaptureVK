#pragma once

#include "ObjectDataBase.h"

namespace Rapture {

// Forward declarations
struct TransformComponent;
struct LightComponent;

struct LightObjectData {
    alignas(16) glm::vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    alignas(16) glm::vec4 direction;     // w = range
    alignas(16) glm::vec4 color;         // w = intensity
    alignas(16) glm::vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};

class LightDataBuffer : public ObjectDataBuffer {
public:
    LightDataBuffer();
    
    // Update function that takes the specific data this buffer needs
    void update(const TransformComponent& transform, const LightComponent& light, uint32_t entityID);
    
    // Override the pure virtual update (can be empty if we always use the parameterized version)
    //void update() override {}
};

}
