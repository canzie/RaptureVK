#pragma once

#include "ObjectDataBase.h"

namespace Rapture {

// Forward declarations
struct TransformComponent;

// Data structures for shaders
struct MeshObjectData {
    alignas(16) glm::mat4 modelMatrix;
    alignas(4) uint32_t flags; // Various mesh flags (e.g., visibility, culling)
};

class MeshDataBuffer : public ObjectDataBuffer {
public:

    MeshDataBuffer(uint32_t frameCount = 1);
    

    void update(const TransformComponent& transform, uint32_t flags, uint32_t frameIndex = 0);

};

}