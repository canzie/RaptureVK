#pragma once

#include "Buffers/Buffers.h"


namespace Rapture {

    class VertexBuffer : public Buffer {
        public:
            VertexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator);
            ~VertexBuffer();

            virtual VkBufferUsageFlags getBufferUsage() override;
            virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

            virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

            
            
    };


}
