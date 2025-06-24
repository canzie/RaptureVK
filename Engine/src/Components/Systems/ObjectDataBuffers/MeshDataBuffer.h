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
    MeshDataBuffer();
    
    // Update function that takes the specific data this buffer needs
    void update(const TransformComponent& transform, uint32_t flags = 0);
    
    // Override the pure virtual update (can be empty if we always use the parameterized version)
    //void update() override {}
};

}