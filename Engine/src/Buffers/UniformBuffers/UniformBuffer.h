#pragma once


#include "Buffers/Buffers.h"


namespace Rapture {


// pretty much a standard buffer with the functionaliy of being able to
// - create a descriptor set
// - setup the VkDescriptorBufferInfo and VkWriteDescriptorSet
// - return the offset and size for a suballocation
// this alloction data will be stored in the instanced material (VmaAllocationInfo)
// it can then do a simple addData with the correct offset and size to update any data


class UniformBuffer : public Buffer {
    public:
        UniformBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, void* data = nullptr);
        ~UniformBuffer();

        void createDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorPool descriptorPool, uint32_t binding);
        VkDescriptorSet getDescriptorSet() const { return m_descriptorSet; }
        
        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;



    private:
        VkDescriptorSet m_descriptorSet;

        
        
};


}


