#include "ObjectDataBase.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/Buffers.h"
#include "WindowContext/Application.h"
#include "Logging/Log.h"

namespace Rapture {

ObjectDataBuffer::ObjectDataBuffer(DescriptorSetBindingLocation bindingLocation, size_t dataSize) {
    // Get the descriptor set and binding
    auto descriptorSet = DescriptorManager::getDescriptorSet(bindingLocation);
    if (descriptorSet) {
        m_descriptorBinding = descriptorSet->getUniformBufferBinding(bindingLocation);
    }

    if (!m_descriptorBinding) {
        RP_CORE_ERROR("ObjectDataBuffer: Failed to get descriptor binding for location {}", static_cast<int>(bindingLocation));
        return;
    }

    // Create uniform buffer
    auto& app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();
    m_buffer = std::make_shared<UniformBuffer>(dataSize, BufferUsage::DYNAMIC, allocator); // TODO change from dynamic to provided

    // Add buffer to descriptor binding
    m_descriptorIndex = m_descriptorBinding->add(m_buffer);
    
    if (m_descriptorIndex == UINT32_MAX) {
        RP_CORE_ERROR("ObjectDataBuffer: Failed to allocate descriptor index");
    }
}

ObjectDataBuffer::~ObjectDataBuffer() {
    if (m_descriptorBinding && m_descriptorIndex != UINT32_MAX) {
        m_descriptorBinding->free(m_descriptorIndex);
    }
}

void ObjectDataBuffer::updateBuffer(const void* data, size_t size) {
    if (!isValid()) {
        RP_CORE_WARN("ObjectDataBuffer: Attempting to update invalid buffer");
        return;
    }

    // Check if data changed to avoid unnecessary GPU updates
    if (hasDataChanged(data, size) || needsUpdate()) {
        m_buffer->addData(const_cast<void*>(data), size, 0);
        markUpdated();
    }
}

bool ObjectDataBuffer::hasDataChanged(const void* data, size_t size) const {
    std::size_t currentHash = calculateHash(data, size);
    if (m_lastDataHash != currentHash) {
        m_lastDataHash = currentHash;
        m_needsUpdate = true;
        return true;
    }
    return false;
}

std::size_t ObjectDataBuffer::calculateHash(const void* data, size_t size) const {
    std::size_t hash = 0;
    const char* bytes = reinterpret_cast<const char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= std::hash<char>{}(bytes[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

}