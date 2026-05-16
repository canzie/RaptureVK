#pragma once

#include "renderer/GPUDataStructs.h"

#include <glm/glm.hpp>

namespace Rapture {

// gets used in the ubo for drawing to the shadowmap textures
struct ShadowMapData {
    alignas(16) glm::mat4 lightViewProjection;
};

struct CSMData {
    alignas(16) glm::mat4 lightViewProjection[MAX_CASCADES];
};

} // namespace Rapture
