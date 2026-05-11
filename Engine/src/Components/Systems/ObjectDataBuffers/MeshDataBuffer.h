#ifndef RAPTURE__MESHDATABUFFER_H
#define RAPTURE__MESHDATABUFFER_H

#include "ObjectDataBase.h"
#include "Components/ComponentsCommon.h"

namespace Rapture {

struct TransformComponent;

struct MeshObjectData {
    alignas(16) glm::mat4 modelMatrix;
    alignas(4) uint32_t flags; // Various mesh flags (e.g., visibility, culling)
};

class MeshDataBuffer : public ObjectDataBuffer {
  public:
    MeshDataBuffer(uint32_t frameCount = 1);

    /**
     * @brief Updates the mesh UBO for the given frame
     * @note Expected to be called every frame, per-frame generation tracking assumes continuous calls
     */
    void onUpdate(const TransformComponent &transform, uint32_t flags, uint32_t frameIndex = 0);

    bool transformChanged = false;

  private:
    std::vector<generation_t> m_lastTransformGenerations;
};

} // namespace Rapture

#endif // RAPTURE__MESHDATABUFFER_H
