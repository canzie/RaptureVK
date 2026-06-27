#ifndef RAPTURE__FRUSTUM_H
#define RAPTURE__FRUSTUM_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Rapture {

class BoundingBox;
class StorageBuffer;

enum class FrustumResult {
    Inside,
    Intersect,
    Outside
};

class Frustum {
  public:
    Frustum() = default;
    ~Frustum();

    Frustum(const Frustum &) = delete;
    Frustum &operator=(const Frustum &) = delete;
    Frustum(Frustum &&) = default;
    Frustum &operator=(Frustum &&) = default;

    void update(const glm::mat4 &projection, const glm::mat4 &view);

    FrustumResult testBoundingBox(const BoundingBox &boundingBox) const;

    const std::array<glm::vec4, 6> &getPlanes() const { return m_planes; }

    void uploadFrustum(uint32_t frameIndex);

    uint32_t getBindlessIndex(uint32_t frameIndex) const;

  private:
    std::array<glm::vec4, 6> m_planes;
    std::vector<std::shared_ptr<StorageBuffer>> m_gpuBuffers;
    std::vector<uint32_t> m_bindlessIndices;
};

} // namespace Rapture

#endif // RAPTURE__FRUSTUM_H
