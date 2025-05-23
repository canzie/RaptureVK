#pragma once

#include "Buffers/Buffers.h"



namespace Rapture {


class IndexBuffer : public Buffer {
    public:
        IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator);
        ~IndexBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;
        
        
};

}
