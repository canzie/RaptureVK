#pragma once

#include "Buffers/Buffers.h"

namespace Rapture {

template<typename T>
class DescriptorSubAllocationBase;


class IndexBuffer : public Buffer {
    public:
        IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, uint32_t indexType);
        IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, VkIndexType indexType);
        ~IndexBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        VkIndexType getIndexType() const { return m_indexType; }
        
        // Get the bindless descriptor index for this buffer
        uint32_t getBindlessIndex();
        
        static DescriptorSubAllocationBase<Buffer>* getBindlessBuffers() { return s_bindlessBuffers.get(); }
        
    private:
        VkIndexType m_indexType;
        uint32_t m_bindlessIndex = UINT32_MAX;
        
        static std::unique_ptr<DescriptorSubAllocationBase<Buffer>> s_bindlessBuffers;
};

}
