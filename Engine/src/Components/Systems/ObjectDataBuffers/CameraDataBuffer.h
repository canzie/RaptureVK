#pragma once

#include "ObjectDataBase.h"

#include "Cameras/CameraCommon.h"

namespace Rapture {

// Forward declarations
struct CameraComponent;
struct TransformComponent;

class CameraDataBuffer : public ObjectDataBuffer {
public:

    CameraDataBuffer(uint32_t frameCount = 1);
    

    void update(const CameraComponent& camera, uint32_t frameIndex = 0);
    

};

} 