#pragma once

#include "Buffers/Buffers.h"

namespace Rapture {



class StorageBuffer : public Buffer {
    public:
        StorageBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, void* data = nullptr);
        ~StorageBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        // Get descriptor buffer info for use in descriptor sets
        VkDescriptorBufferInfo getDescriptorBufferInfo() const;

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        
};


}
