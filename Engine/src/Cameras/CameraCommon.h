#pragma once

#include <glm/glm.hpp>

namespace Rapture {

    // Camera/View uniform buffer object (binding 0)
    struct CameraUniformBufferObject {
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 proj = glm::mat4(1.0f);
    };


}


