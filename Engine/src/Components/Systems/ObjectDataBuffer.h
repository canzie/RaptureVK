#pragma once

#include "Buffers/Descriptors/DescriptorBinding.h"
#include <memory>
#include <glm/glm.hpp>
#include <cstdint>


namespace Rapture {

// Forward declarations
class Buffer;
class UniformBuffer;

struct TransformComponent;
struct LightComponent;
enum class LightType;

// Base class for object data buffers
class ObjectDataBufferBase {
public:
    virtual ~ObjectDataBufferBase() = default;
    virtual uint32_t getDescriptorIndex() const = 0;
    virtual bool isValid() const = 0;

protected:
    // Shared descriptor array allocation for all object buffers
    static std::shared_ptr<DescriptorBindingUniformBuffer> s_descriptorAllocation;

private:
    // Track changes to reduce GPU updates
    mutable std::size_t m_lastDataHash = 0;
    mutable bool m_needsUpdate = true;

protected:
    bool hasDataChanged(std::size_t currentHash) const {
        if (m_lastDataHash != currentHash) {
            m_lastDataHash = currentHash;
            m_needsUpdate = true;
            return true;
        }
        return false;
    }

    void markUpdated() const { m_needsUpdate = false; }
    bool needsUpdate() const { return m_needsUpdate; }
};

// Mesh object data structure for shaders
struct MeshObjectData {
    alignas(16) glm::mat4 modelMatrix;
    alignas(4) uint32_t flags; // Various mesh flags (e.g., visibility, culling)

};

// Light data structure for shader
struct LightObjectData {
    alignas(16) glm::vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
    alignas(16) glm::vec4 direction;     // w = range
    alignas(16) glm::vec4 color;         // w = intensity
    alignas(16) glm::vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = entity id, w = unused
};

// Template-based object data buffer for type safety
template<typename ObjectDataType>
class ObjectDataBuffer : public ObjectDataBufferBase {
public:
    ObjectDataBuffer();
    ~ObjectDataBuffer();

    uint32_t getDescriptorIndex() const override;
    bool isValid() const override;
    void updateData(const ObjectDataType& data);

private:
    std::shared_ptr<UniformBuffer> m_buffer;
    uint32_t m_descriptorIndex = UINT32_MAX;

    std::size_t calculateHash(const ObjectDataType& data) const;
};

// Specialized mesh data buffer
class MeshDataBuffer : public ObjectDataBuffer<MeshObjectData> {
public:
    void updateFromComponents(const TransformComponent& transform, uint32_t flags = 0);
};

// Specialized light data buffer
class LightDataBuffer : public ObjectDataBuffer<LightObjectData> {
public:
    void updateFromComponents(const TransformComponent& transformComp, 
                            const LightComponent& lightComp,
                            uint32_t entityID);
};

// Extern template declarations to prevent multiple instantiations
extern template class ObjectDataBuffer<MeshObjectData>;
extern template class ObjectDataBuffer<LightObjectData>;

} // namespace Rapture