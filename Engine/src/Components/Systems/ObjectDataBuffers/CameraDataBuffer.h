#pragma once

#include "ObjectDataBase.h"

#include "Cameras/CameraCommon.h"

namespace Rapture {

// Forward declarations
struct CameraComponent;
struct TransformComponent;

class CameraDataBuffer : public ObjectDataBuffer {
public:
    CameraDataBuffer();
    
    // Update from specific camera component
    void update(const CameraComponent& camera);
    
    // Override the pure virtual update (can be empty since we use parameterized versions)
    //void update() override {}
};

} 