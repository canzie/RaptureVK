#include "CameraDataBuffer.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "cameras/CameraCommon.h"
#include "components/Components.h"
#include "logging/Log.h"
#include "scenes/Scene.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Rapture {

CameraDataBuffer::CameraDataBuffer(uint32_t frameCount)
    : ObjectDataBuffer(DescriptorSetBindingLocation::CAMERA_UBO, sizeof(CameraUniformBufferObject), frameCount)
{
}

void CameraDataBuffer::onUpdate(const CameraComponent &camera, uint32_t frameIndex)
{
    CameraUniformBufferObject ubo{};

    // Use the camera's matrices
    ubo.view = camera.camera.getViewMatrix();
    ubo.proj = camera.camera.getProjectionMatrix();
    ubo.proj[1][1] *= -1;

    updateBuffer(&ubo, sizeof(CameraUniformBufferObject), frameIndex);
}

} // namespace Rapture
