#ifndef RAPTURE__LIGHTDATABUFFER_H
#define RAPTURE__LIGHTDATABUFFER_H

#include "components/ComponentsCommon.h"
#include "ObjectDataBase.h"

namespace Rapture {

// Forward declarations
struct TransformComponent;
struct LightComponent;

struct LightObjectData {
    alignas(16) glm::vec4 position;   // w = light type (0 = point, 1 = directional, 2 = spot)
    alignas(16) glm::vec4 direction;  // w = range
    alignas(16) glm::vec4 color;      // w = intensity
    alignas(16) glm::vec4 spotAngles; // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};

class LightDataBuffer : public ObjectDataBuffer {
  public:
    LightDataBuffer(uint32_t frameCount = 1);

    /**
     * @brief Updates the light UBO for the given frame
     * @note Expected to be called every frame, per-frame generation tracking assumes continuous calls
     */
    void onUpdate(const TransformComponent &transform, const LightComponent &light, uint32_t entityID, uint32_t frameIndex = 0);

  private:
    std::vector<generation_t> m_lastTransformGenerations;
    std::vector<generation_t> m_lastLightGenerations;
};

} // namespace Rapture

#endif // RAPTURE__LIGHTDATABUFFER_H
