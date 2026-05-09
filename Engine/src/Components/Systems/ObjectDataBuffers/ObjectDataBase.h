#ifndef RAPTURE__OBJECTDATABASE_H
#define RAPTURE__OBJECTDATABASE_H

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Rapture {

// Forward declarations
class UniformBuffer;
class DescriptorBindingUniformBuffer;
enum class DescriptorSetBindingLocation;

// Base class for all object data buffers
class ObjectDataBuffer {
  public:
    virtual ~ObjectDataBuffer();

    // Common interface
    uint32_t getDescriptorIndex(uint32_t frameIndex = 0) const;
    bool isValid(uint32_t frameIndex = 0) const;

    // Frame management
    uint32_t getFrameCount() const { return m_frameCount; }
    void setCurrentFrame(uint32_t frameIndex);

  protected:
    // Constructor for derived classes
    // frameCount: Number of buffer copies for frames in flight (default = 1)
    ObjectDataBuffer(DescriptorSetBindingLocation bindingLocation, size_t dataSize, uint32_t frameCount = 1);

    // Protected method for derived classes to update their buffer data
    void updateBuffer(const void *data, size_t size, uint32_t frameIndex = 0);

  private:
    std::vector<std::shared_ptr<UniformBuffer>> m_buffers;
    std::shared_ptr<DescriptorBindingUniformBuffer> m_descriptorBinding;
    std::vector<uint32_t> m_descriptorIndices;

    uint32_t m_frameCount;
    uint32_t m_currentFrame = 0;

    // Change tracking to avoid unnecessary GPU updates (per frame)
    mutable std::vector<std::size_t> m_lastDataHashes;
    mutable std::vector<bool> m_needsUpdate;

    // Helper methods for change tracking
    bool hasDataChanged(const void *data, size_t size, uint32_t frameIndex) const;
    void markUpdated(uint32_t frameIndex) const { m_needsUpdate[frameIndex] = false; }
    bool needsUpdate(uint32_t frameIndex) const { return m_needsUpdate[frameIndex]; }
    std::size_t calculateHash(const void *data, size_t size) const;
};

} // namespace Rapture

#endif // RAPTURE__OBJECTDATABASE_H
