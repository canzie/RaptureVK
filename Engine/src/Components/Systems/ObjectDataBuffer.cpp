#include "ObjectDataBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "WindowContext/Application.h"
#include "Components/Components.h"
#include "Logging/Log.h"
#include <cmath>

namespace Rapture {

// Static member definitions
std::shared_ptr<DescriptorBindingUniformBuffer> ObjectDataBufferBase::s_meshDataAllocation = nullptr;
std::shared_ptr<DescriptorBindingUniformBuffer> ObjectDataBufferBase::s_lightDataAllocation = nullptr;

// Template instantiations for the types we use
template class ObjectDataBuffer<MeshObjectData>;
template class ObjectDataBuffer<LightObjectData>;

// Template implementations
template<typename ObjectDataType>
ObjectDataBuffer<ObjectDataType>::ObjectDataBuffer() {
    if (!s_descriptorAllocation) {
        s_descriptorAllocation = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::MESH_DATA_UBO)->getUniformBufferBinding(DescriptorSetBindingLocation::MESH_DATA_UBO);
    }

    auto& app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();


    // Get an index in the descriptor array
    if (s_descriptorAllocation) {
        m_descriptorIndex = s_descriptorAllocation->allocate(m_buffer);
    }
}

template<typename ObjectDataType>
ObjectDataBuffer<ObjectDataType>::~ObjectDataBuffer() {
    if (s_descriptorAllocation && m_descriptorIndex != UINT32_MAX) {
        s_descriptorAllocation->free(m_descriptorIndex);
    }
}

template<typename ObjectDataType>
uint32_t ObjectDataBuffer<ObjectDataType>::getDescriptorIndex() const {
    return m_descriptorIndex;
}

template<typename ObjectDataType>
bool ObjectDataBuffer<ObjectDataType>::isValid() const {
    return m_buffer && m_descriptorIndex != UINT32_MAX;
}

template<typename ObjectDataType>
void ObjectDataBuffer<ObjectDataType>::updateData(const ObjectDataType& data) {
    if (!isValid()) return;

    // Calculate hash to check if data changed
    std::size_t currentHash = calculateHash(data);
    if (hasDataChanged(currentHash) || needsUpdate()) {
        m_buffer->addData(const_cast<ObjectDataType*>(&data), sizeof(ObjectDataType), 0);
        markUpdated();
    }
}

template<typename ObjectDataType>
std::size_t ObjectDataBuffer<ObjectDataType>::calculateHash(const ObjectDataType& data) const {
    // Simple hash of the entire data structure
    std::size_t hash = 0;
    const char* bytes = reinterpret_cast<const char*>(&data);
    for (size_t i = 0; i < sizeof(ObjectDataType); ++i) {
        hash ^= std::hash<char>{}(bytes[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

// MeshDataBuffer implementation
void MeshDataBuffer::updateFromComponents(const TransformComponent& transform, uint32_t flags) {
    MeshObjectData data{};
    data.modelMatrix = transform.transformMatrix();
    data.flags = flags;

    updateData(data);
}

// LightDataBuffer implementation
void LightDataBuffer::updateFromComponents(const TransformComponent& transformComp, 
                                         const LightComponent& lightComp,
                                         uint32_t entityID) {
    if (!lightComp.isActive) {
        return;
    }

    LightObjectData lightData{};

    // Position and light type
    glm::vec3 position = transformComp.translation();
    float lightTypeFloat = static_cast<float>(lightComp.type);
    
    // For directional lights, position is irrelevant - use a default position
    // This ensures the light behaves purely based on direction
    if (lightComp.type == LightType::Directional) {
        position = glm::vec3(0.0f, 0.0f, 0.0f); // Position doesn't matter for directional lights
    }
    
    lightData.position = glm::vec4(position, lightTypeFloat);
    
    // Direction and range
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f); // Default forward direction
    if (lightComp.type == LightType::Directional || lightComp.type == LightType::Spot) {
        glm::quat rotationQuat = transformComp.transforms.getRotationQuat();
        direction = glm::normalize(rotationQuat * glm::vec3(0, 0, -1)); // Forward vector
    }
    lightData.direction = glm::vec4(direction, lightComp.range);
    
    // Color and intensity
    lightData.color = glm::vec4(lightComp.color, lightComp.intensity);
    
    // Spot light angles
    if (lightComp.type == LightType::Spot) {
        float innerCos = std::cos(lightComp.innerConeAngle);
        float outerCos = std::cos(lightComp.outerConeAngle);
        lightData.spotAngles = glm::vec4(innerCos, outerCos, 0.0f, 0.0f);
    } else {
        lightData.spotAngles = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    
    lightData.spotAngles.z = static_cast<float>(entityID);

    updateData(lightData);
}

} // namespace Rapture 