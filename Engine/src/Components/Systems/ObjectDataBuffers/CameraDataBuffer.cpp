#include "CameraDataBuffer.h"
#include "Components/Components.h"
#include "Cameras/CameraCommon.h"
#include "Scenes/Scene.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Logging/Log.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Rapture {

CameraDataBuffer::CameraDataBuffer() 
    : ObjectDataBuffer(DescriptorSetBindingLocation::CAMERA_UBO, sizeof(CameraUniformBufferObject)) {
}


void CameraDataBuffer::update(const CameraComponent& camera) {
    CameraUniformBufferObject ubo{};


    // Use the camera's matrices
    ubo.view = camera.camera.getViewMatrix();
    ubo.proj = camera.camera.getProjectionMatrix();
    ubo.proj[1][1] *= -1;

    updateBuffer(&ubo, sizeof(CameraUniformBufferObject));
}

} 