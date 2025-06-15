#pragma once

#include "Buffers/Buffers.h"
#include "Buffers/VertexBuffers/BufferLayout.h"

namespace Rapture {

template<typename T>
class DescriptorSubAllocationBase;

class VertexBuffer : public Buffer {
    public:
        VertexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator);
        ~VertexBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        void setBufferLayout(const BufferLayout& bufferLayout);

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        BufferLayout& getBufferLayout() { return m_bufferLayout; }
        
        // Get the bindless descriptor index for this buffer
        uint32_t getBindlessIndex();
        
        static DescriptorSubAllocationBase<Buffer>* getBindlessBuffers() { return s_bindlessBuffers.get(); }

    private:
        BufferLayout m_bufferLayout;
        uint32_t m_bindlessIndex = UINT32_MAX;
        
        static std::unique_ptr<DescriptorSubAllocationBase<Buffer>> s_bindlessBuffers;
};

}
