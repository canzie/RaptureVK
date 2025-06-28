#pragma once

#include "Buffers/Buffers.h"
#include "Buffers/VertexBuffers/BufferLayout.h"
#include "Buffers/BufferPool.h"
#include "Buffers/Descriptors/DescriptorBinding.h"

namespace Rapture {

class VertexBuffer : public Buffer {
    public:
        VertexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator);
        VertexBuffer(BufferAllocationRequest& request, VmaAllocator allocator, void* data);
        ~VertexBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        void setBufferLayout(const BufferLayout& bufferLayout);

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        BufferLayout& getBufferLayout() { return m_bufferLayout; }
        
        // Get the bindless descriptor index for this buffer
        uint32_t getBindlessIndex();
        
        static std::shared_ptr<DescriptorBindingSSBO> getBindlessBuffers() { return s_bindlessBuffers; }

    private:
        BufferLayout m_bufferLayout;
        uint32_t m_bindlessIndex = UINT32_MAX;
        
        static std::shared_ptr<DescriptorBindingSSBO> s_bindlessBuffers;
};

}
