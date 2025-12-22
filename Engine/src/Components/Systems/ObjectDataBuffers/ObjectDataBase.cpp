#include "ObjectDataBase.h"
#include "Buffers/Buffers.h"
#include "Buffers/Descriptors/DescriptorBinding.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

namespace Rapture {

ObjectDataBuffer::ObjectDataBuffer(DescriptorSetBindingLocation bindingLocation, size_t dataSize, uint32_t frameCount)
    : m_frameCount(frameCount)
{

    // Get the descriptor set and binding
    auto descriptorSet = DescriptorManager::getDescriptorSet(bindingLocation);
    if (descriptorSet) {
        m_descriptorBinding = descriptorSet->getUniformBufferBinding(bindingLocation);
    }

    if (!m_descriptorBinding) {
        RP_CORE_ERROR("Failed to get descriptor binding for location {}", static_cast<int>(bindingLocation));
        return;
    }

    // Create multiple uniform buffers for frames in flight
    auto &app = Application::getInstance();
    VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();

    m_buffers.resize(m_frameCount);
    m_descriptorIndices.resize(m_frameCount);
    m_lastDataHashes.resize(m_frameCount, 0);
    m_needsUpdate.resize(m_frameCount, true);

    for (uint32_t i = 0; i < m_frameCount; ++i) {
        m_buffers[i] = std::make_shared<UniformBuffer>(dataSize, BufferUsage::DYNAMIC, allocator);

        // Add buffer to descriptor binding
        m_descriptorIndices[i] = m_descriptorBinding->add(m_buffers[i]);

        if (m_descriptorIndices[i] == UINT32_MAX) {
            RP_CORE_ERROR("Failed to allocate descriptor index for frame {}", i);
        }
    }
}

ObjectDataBuffer::~ObjectDataBuffer()
{
    if (m_descriptorBinding) {
        for (uint32_t i = 0; i < m_frameCount; ++i) {
            if (m_descriptorIndices[i] != UINT32_MAX) {
                m_descriptorBinding->free(m_descriptorIndices[i]);
            }
        }
    }
}

uint32_t ObjectDataBuffer::getDescriptorIndex(uint32_t frameIndex) const
{
    // If frameIndex is 0 and we have multiple frames, use current frame
    if (frameIndex == 0 && m_frameCount > 1) {
        frameIndex = m_currentFrame;
    }

    if (frameIndex >= m_frameCount) {
        RP_CORE_WARN("Frame index {} out of bounds (max: {})", frameIndex, m_frameCount - 1);
        return UINT32_MAX;
    }
    return m_descriptorIndices[frameIndex];
}

bool ObjectDataBuffer::isValid(uint32_t frameIndex) const
{
    // If frameIndex is 0 and we have multiple frames, use current frame
    if (frameIndex == 0 && m_frameCount > 1) {
        frameIndex = m_currentFrame;
    }

    if (frameIndex >= m_frameCount) {
        return false;
    }
    return m_buffers[frameIndex] && m_descriptorIndices[frameIndex] != UINT32_MAX;
}

void ObjectDataBuffer::setCurrentFrame(uint32_t frameIndex)
{
    if (frameIndex < m_frameCount) {
        m_currentFrame = frameIndex;
    } else {
        RP_CORE_WARN("Attempted to set frame index {} out of bounds (max: {})", frameIndex, m_frameCount - 1);
    }
}

void ObjectDataBuffer::updateBuffer(const void *data, size_t size, uint32_t frameIndex)
{
    // If frameIndex is 0 and we have multiple frames, use current frame
    if (frameIndex == 0 && m_frameCount > 1) {
        frameIndex = m_currentFrame;
    }

    if (frameIndex >= m_frameCount) {
        RP_CORE_WARN("Frame index {} out of bounds (max: {})", frameIndex, m_frameCount - 1);
        return;
    }

    if (!isValid(frameIndex)) {
        RP_CORE_WARN("Attempting to update invalid buffer for frame {}", frameIndex);
        return;
    }

    // Check if data changed to avoid unnecessary GPU updates
    if (hasDataChanged(data, size, frameIndex) || needsUpdate(frameIndex)) {
        m_buffers[frameIndex]->addData(const_cast<void *>(data), size, 0);
        markUpdated(frameIndex);
    }
}

bool ObjectDataBuffer::hasDataChanged(const void *data, size_t size, uint32_t frameIndex) const
{
    if (frameIndex >= m_frameCount) {
        return false;
    }

    std::size_t currentHash = calculateHash(data, size);
    if (m_lastDataHashes[frameIndex] != currentHash) {
        m_lastDataHashes[frameIndex] = currentHash;
        m_needsUpdate[frameIndex] = true;
        return true;
    }
    return false;
}

std::size_t ObjectDataBuffer::calculateHash(const void *data, size_t size) const
{
    std::size_t hash = 0;
    const char *bytes = reinterpret_cast<const char *>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= std::hash<char>{}(bytes[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

} // namespace Rapture
