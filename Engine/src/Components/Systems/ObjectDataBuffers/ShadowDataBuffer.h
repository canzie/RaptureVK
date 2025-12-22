#pragma once

#include "ObjectDataBase.h"

namespace Rapture {

// Forward declarations
struct LightComponent;
struct TransformComponent;
struct ShadowComponent;
struct CascadedShadowComponent;
class ShadowMap;
class CascadedShadowMap;

class ShadowDataBuffer : public ObjectDataBuffer {
  public:
    ShadowDataBuffer(uint32_t frameCount = 1);

    // Update from shadow component (regular shadow map)
    // frameIndex: Frame to update (0 = current frame for multi-frame buffers)
    void update(const LightComponent &light, const ShadowComponent &shadow, uint32_t entityID, uint32_t frameIndex = 0);
    void update(const LightComponent &light, ShadowMap *shadowMap, uint32_t entityID, uint32_t frameIndex = 0);

    // Update from cascaded shadow component
    // frameIndex: Frame to update (0 = current frame for multi-frame buffers)
    void update(const LightComponent &light, const CascadedShadowComponent &shadow, uint32_t entityID, uint32_t frameIndex = 0);
    void update(const LightComponent &light, CascadedShadowMap *cascadedShadowMap, uint32_t entityID, uint32_t frameIndex = 0);
};

} // namespace Rapture