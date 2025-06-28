#pragma once

#include "Buffers/Buffers.h"
#include "Buffers/BufferPool.h"
#include "Buffers/Descriptors/DescriptorBinding.h"


namespace Rapture {



class IndexBuffer : public Buffer {
    public:
        IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, uint32_t indexType);
        IndexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator, VkIndexType indexType);
        IndexBuffer(BufferAllocationRequest& request, VmaAllocator allocator, void* data);
        ~IndexBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        VkIndexType getIndexType() const { return m_indexType; }
        
        // Get the bindless descriptor index for this buffer
        uint32_t getBindlessIndex();
        
        static std::shared_ptr<DescriptorBindingSSBO> getBindlessBuffers() { return s_bindlessBuffers; }
        
    private:
        VkIndexType m_indexType;
        uint32_t m_bindlessIndex = UINT32_MAX;


        static std::shared_ptr<DescriptorBindingSSBO> s_bindlessBuffers;
};

}
