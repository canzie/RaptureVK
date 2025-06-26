#include "Renderer/MDIBatch.h"
#include "Meshes/Mesh.h"
#include "Buffers/Buffers.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "WindowContext/Application.h"
#include "Logging/Log.h"

#include <stdexcept>

namespace Rapture
{
    static constexpr uint32_t INITIAL_BATCH_SIZE = 128;

    MDIBatch::MDIBatch(std::shared_ptr<BufferAllocation> vboArena, std::shared_ptr<BufferAllocation> iboArena, BufferLayout& bufferLayout, VkIndexType indexType) 
        : m_vboArenaId(vboArena->parentArena->id), m_iboArenaId(iboArena->parentArena->id), m_bufferLayout(bufferLayout), m_indexType(indexType) {
        m_vertexBuffer = vboArena->getBuffer();
        m_indexBuffer = iboArena->getBuffer();
        // Buffers will be created during uploadBuffers()
    }

    MDIBatch::~MDIBatch() {
        // Free the descriptor binding if it was allocated
        if (m_batchInfoBufferIndex != UINT32_MAX) {
            auto descriptorSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::MDI_INDEXED_INFO_SSBOS);
            if (descriptorSet) {
                auto ssboBinding = descriptorSet->getSSBOBinding(DescriptorSetBindingLocation::MDI_INDEXED_INFO_SSBOS);
                if (ssboBinding) {
                    ssboBinding->free(m_batchInfoBufferIndex);
                }
            }
        }
    }

    void MDIBatch::addObject(const Mesh& mesh, uint32_t meshIndex, uint32_t materialIndex) {

        auto vboAlloc = mesh.getVertexAllocation();
        auto iboAlloc = mesh.getIndexAllocation();

        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = mesh.getIndexCount();
        cmd.instanceCount = 1;
        cmd.firstIndex = iboAlloc->getFirstElementIndex();  // Direct element index from BufferPool
        cmd.vertexOffset = static_cast<int32_t>(vboAlloc->getFirstElementIndex());  // Direct element index from BufferPool
        cmd.firstInstance = m_cpuIndirectCommands.size(); // This will be the index into the batch info buffer

        m_cpuIndirectCommands.push_back(cmd);
        m_cpuObjectInfo.push_back({meshIndex, materialIndex});
    }

    void MDIBatch::uploadBuffers() {
        if (m_cpuIndirectCommands.empty()) return;

        // Create buffers if they don't exist yet
        if (!m_buffersCreated) {
            uint32_t requiredSize = m_cpuIndirectCommands.size();
            auto& app = Application::getInstance();
            VmaAllocator allocator = app.getVulkanContext().getVmaAllocator();
            
            // Create indirect buffer with additional indirect buffer usage flag
            m_indirectBuffer = std::make_shared<StorageBuffer>(requiredSize * sizeof(VkDrawIndexedIndirectCommand), 
                                                              BufferUsage::DYNAMIC, 
                                                              allocator, 
                                                              VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

            // Create batch info buffer
            m_batchInfoBuffer = std::make_shared<StorageBuffer>(requiredSize * sizeof(ObjectInfo), 
                                                               BufferUsage::DYNAMIC, 
                                                               allocator);
            
            // Add batch info buffer to descriptor set and get index
            auto descriptorSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::MDI_INDEXED_INFO_SSBOS);
            if (descriptorSet) {
                auto ssboBinding = descriptorSet->getSSBOBinding(DescriptorSetBindingLocation::MDI_INDEXED_INFO_SSBOS);
                if (ssboBinding) {
                    m_batchInfoBufferIndex = ssboBinding->add(m_batchInfoBuffer);
                    if (m_batchInfoBufferIndex == UINT32_MAX) {
                        RP_CORE_ERROR("Failed to add batch info buffer to descriptor set");
                    }
                }
            }
            
            m_buffersCreated = true;
        }

        m_indirectBuffer->addData(m_cpuIndirectCommands.data(), m_cpuIndirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand), 0);
        m_batchInfoBuffer->addData(m_cpuObjectInfo.data(), m_cpuObjectInfo.size() * sizeof(ObjectInfo), 0);
    }

    void MDIBatch::clear() {
        m_cpuIndirectCommands.clear();
        m_cpuObjectInfo.clear();
    }

    std::shared_ptr<StorageBuffer> MDIBatch::getIndirectBuffer() {
        if (!m_buffersCreated) {
            RP_CORE_ERROR("MDIBatch::getIndirectBuffer() called before uploadBuffers()");
            return nullptr;
        }
        return m_indirectBuffer;
    }

    std::shared_ptr<StorageBuffer> MDIBatch::getBatchInfoBuffer() {
        if (!m_buffersCreated) {
            RP_CORE_ERROR("MDIBatch::getBatchInfoBuffer() called before uploadBuffers()");
            return nullptr;
        }
        return m_batchInfoBuffer;
    }

    uint32_t MDIBatch::getBatchInfoBufferIndex() const {
        if (!m_buffersCreated) {
            RP_CORE_ERROR("MDIBatch::getBatchInfoBufferIndex() called before uploadBuffers()");
            return UINT32_MAX;
        }
        return m_batchInfoBufferIndex;
    }

    MDIBatchMap::MDIBatchMap() {}

    void MDIBatchMap::beginFrame() {
        for (auto& [key, batch] : m_batches) {
            batch->clear();
        }
    }

    MDIBatch* MDIBatchMap::obtainBatch(std::shared_ptr<BufferAllocation> vboArena, std::shared_ptr<BufferAllocation> iboArena, BufferLayout& bufferLayout, VkIndexType indexType) {
        uint64_t key = ((uint64_t)vboArena->parentArena->id << 32) | iboArena->parentArena->id;
        auto it = m_batches.find(key);
        if (it != m_batches.end()) {
            return it->second.get();
        }

        auto newBatch = std::make_unique<MDIBatch>(vboArena, iboArena, bufferLayout, indexType);
        MDIBatch* batchPtr = newBatch.get();
        m_batches[key] = std::move(newBatch);
        return batchPtr;
    }
} 