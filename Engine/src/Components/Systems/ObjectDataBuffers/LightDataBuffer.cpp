#include "LightDataBuffer.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Components/Components.h"
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

LightDataBuffer::LightDataBuffer(uint32_t frameCount)
    : ObjectDataBuffer(DescriptorSetBindingLocation::LIGHTS_UBO, sizeof(LightObjectData), frameCount)
{
}

void LightDataBuffer::update(const TransformComponent &transform, const LightComponent &light, uint32_t entityID,
                             uint32_t frameIndex)
{
    if (!light.isActive) {
        return;
    }

    LightObjectData data{};

    // Position and light type
    glm::vec3 position = transform.translation();
    float lightTypeFloat = static_cast<float>(light.type);

    // For directional lights, position is irrelevant
    if (light.type == LightType::Directional) {
        position = glm::vec3(0.0f);
    }

    data.position = glm::vec4(position, lightTypeFloat);

    // Direction and range
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f); // Default forward
    if (light.type == LightType::Directional || light.type == LightType::Spot) {
        glm::quat rotationQuat = transform.transforms.getRotationQuat();
        direction = glm::normalize(rotationQuat * glm::vec3(0, 0, -1));
    }
    data.direction = glm::vec4(direction, light.range);

    // Color and intensity
    data.color = glm::vec4(light.color, light.intensity);

    // Spot light angles
    if (light.type == LightType::Spot) {
        float innerCos = std::cos(light.innerConeAngle);
        float outerCos = std::cos(light.outerConeAngle);
        data.spotAngles = glm::vec4(innerCos, outerCos, 0.0f, 0.0f);
    } else {
        data.spotAngles = glm::vec4(0.0f);
    }

    data.spotAngles.z = static_cast<float>(entityID);

    updateBuffer(&data, sizeof(LightObjectData), frameIndex);
}

} // namespace Rapture