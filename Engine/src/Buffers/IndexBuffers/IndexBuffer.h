#pragma once

#include "Buffers/Buffers.h"



namespace Rapture {


class IndexBuffer : public Buffer {
    public:
        IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, uint32_t indexType);
        IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, VkIndexType indexType);
        ~IndexBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        VkIndexType getIndexType() const { return m_indexType; }
        
    private:
        VkIndexType m_indexType;
        
};

}
