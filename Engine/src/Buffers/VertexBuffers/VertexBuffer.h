#pragma once

#include "Buffers/Buffers.h"
#include "Buffers/VertexBuffers/BufferLayout.h"

namespace Rapture {



class VertexBuffer : public Buffer {
    public:
        VertexBuffer(VkDeviceSize size, BufferUsage usage, VmaAllocator allocator);
        ~VertexBuffer();

        virtual VkBufferUsageFlags getBufferUsage() override;
        virtual VkMemoryPropertyFlags getMemoryPropertyFlags() override;

        void setBufferLayout(const BufferLayout& bufferLayout);

        virtual void addDataGPU(void* data, VkDeviceSize size, VkDeviceSize offset) override;

        BufferLayout& getBufferLayout() { return m_bufferLayout; }
    private:
        BufferLayout m_bufferLayout;

        
};


}
