#include "ShadowDataBuffer.h"
#include "Components/Components.h"
#include "Renderer/Shadows/ShadowCommon.h"
#include "Scenes/Scene.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Logging/Log.h"

namespace Rapture {

ShadowDataBuffer::ShadowDataBuffer(uint32_t frameCount) 
    : ObjectDataBuffer(DescriptorSetBindingLocation::SHADOW_DATA_UBO, sizeof(ShadowBufferData), frameCount) {
}

void ShadowDataBuffer::update(const LightComponent& light, const ShadowComponent& shadow, uint32_t entityID, uint32_t frameIndex) {
    if (!shadow.shadowMap || !shadow.isActive) {
        return;
    }

    update(light, shadow.shadowMap.get(), entityID, frameIndex);
}

void ShadowDataBuffer::update(const LightComponent &light, ShadowMap* shadowMap, uint32_t entityID, uint32_t frameIndex) {
    if (!shadowMap) {
        return;
    }

    // Create shadow buffer data for this individual shadow map
    ShadowBufferData shadowData{};
    shadowData.type = static_cast<int>(light.type);
    shadowData.cascadeCount = 1;
    shadowData.lightIndex = entityID;
    shadowData.textureHandle = shadowMap->getTextureHandle();
    shadowData.cascadeMatrices[0] = shadowMap->getLightViewProjection();
    shadowData.cascadeSplitsViewSpace[0] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    updateBuffer(&shadowData, sizeof(ShadowBufferData), frameIndex);

}

void ShadowDataBuffer::update(const LightComponent& light, const CascadedShadowComponent& shadow, uint32_t entityID, uint32_t frameIndex) {
    if (!shadow.cascadedShadowMap || !shadow.isActive) {
        return;
    }

    update(light, shadow.cascadedShadowMap.get(), entityID, frameIndex);
}

void ShadowDataBuffer::update(const LightComponent &light, CascadedShadowMap *cascadedShadowMap, uint32_t entityID, uint32_t frameIndex) {
    if (!cascadedShadowMap) {
        return;
    }

    // Create shadow buffer data for this individual cascaded shadow map
    ShadowBufferData shadowData{};
    shadowData.type = static_cast<int>(light.type);
    shadowData.cascadeCount = cascadedShadowMap->getNumCascades();
    shadowData.lightIndex = entityID;
    shadowData.textureHandle = cascadedShadowMap->getTextureHandle();

    // # splits will always be numcascades + 1
    auto splits = cascadedShadowMap->getCascadeSplits();

    // Populate cascade data from the shadow map
    for (uint8_t i = 0; i < cascadedShadowMap->getNumCascades(); i++) {
        shadowData.cascadeMatrices[i] = cascadedShadowMap->getLightViewProjections()[i];
        shadowData.cascadeSplitsViewSpace[i] = glm::vec4(splits[i], splits[i+1], 0.0f, -1.0f);
    }

    updateBuffer(&shadowData, sizeof(ShadowBufferData), frameIndex);
}
} 