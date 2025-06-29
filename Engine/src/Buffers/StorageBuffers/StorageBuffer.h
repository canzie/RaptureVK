#pragma once

#include "Buffers/Buffers.h"

namespace Rapture {



class StorageBuffer : public Buffer {
    public:
        StorageBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, void* data = nullptr);
        StorageBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, VkBufferUsageFlags additionalUsageFlags, void* data = nullptr);
        ~StorageBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;


        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        uint32_t getBindlessIndex();

    private:
        // index in the bindless ssbo binding
        uint32_t m_bindlessIndex = UINT32_MAX;
        
};


}
