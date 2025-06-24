#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <cstdint>

namespace Rapture {



// Forward declarations
class UniformBuffer;
class DescriptorBindingUniformBuffer;
enum class DescriptorSetBindingLocation;

// Base class for all object data buffers
class ObjectDataBuffer {
public:
    virtual ~ObjectDataBuffer();

    // Pure virtual update function - each derived class implements its own
    //virtual void update() = 0;
    
    // Common interface
    uint32_t getDescriptorIndex() const { return m_descriptorIndex; }
    bool isValid() const { return m_buffer && m_descriptorIndex != UINT32_MAX; }

protected:
    // Constructor for derived classes
    ObjectDataBuffer(DescriptorSetBindingLocation bindingLocation, size_t dataSize);
    
    // Protected method for derived classes to update their buffer data
    void updateBuffer(const void* data, size_t size);

private:
    std::shared_ptr<UniformBuffer> m_buffer;
    std::shared_ptr<DescriptorBindingUniformBuffer> m_descriptorBinding;
    uint32_t m_descriptorIndex = UINT32_MAX;
    
    // Change tracking to avoid unnecessary GPU updates
    mutable std::size_t m_lastDataHash = 0;
    mutable bool m_needsUpdate = true;
    
    // Helper methods for change tracking
    bool hasDataChanged(const void* data, size_t size) const;
    void markUpdated() const { m_needsUpdate = false; }
    bool needsUpdate() const { return m_needsUpdate; }
    std::size_t calculateHash(const void* data, size_t size) const;
};

}