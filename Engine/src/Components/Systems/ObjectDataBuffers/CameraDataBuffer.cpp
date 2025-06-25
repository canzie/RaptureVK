#include "CameraDataBuffer.h"
#include "Components/Components.h"
#include "Cameras/CameraCommon.h"
#include "Scenes/Scene.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Logging/Log.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Rapture {

CameraDataBuffer::CameraDataBuffer(uint32_t frameCount) 
    : ObjectDataBuffer(DescriptorSetBindingLocation::CAMERA_UBO, sizeof(CameraUniformBufferObject), frameCount) {
}

void CameraDataBuffer::update(const CameraComponent& camera, uint32_t frameIndex) {
    CameraUniformBufferObject ubo{};

    // Use the camera's matrices
    ubo.view = camera.camera.getViewMatrix();
    ubo.proj = camera.camera.getProjectionMatrix();
    ubo.proj[1][1] *= -1;

    updateBuffer(&ubo, sizeof(CameraUniformBufferObject), frameIndex);
}

} 