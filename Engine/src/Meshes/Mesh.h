#pragma once


#include <string>
#include <vector>

#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"
#include "Buffers/VertexBuffers/BufferLayout.h"



namespace Rapture
{


struct AllocatorParams {
    void* vertexData = nullptr;
    uint32_t vertexDataSize = 0;
    void* indexData = nullptr;
    uint32_t indexDataSize = 0;
    uint32_t indexCount = 0;
    uint32_t indexType = 0;
    
    BufferLayout bufferLayout;
};

class Mesh
{

public:
    Mesh(AllocatorParams& params);
    Mesh();
    ~Mesh();

    void setMeshData(AllocatorParams& params);

    std::shared_ptr<VertexBuffer> getVertexBuffer() const { return m_vertexBuffer; }
    std::shared_ptr<IndexBuffer> getIndexBuffer() const { return m_indexBuffer; }

    uint32_t getIndexCount() const { return m_indexCount; }
    
    //static std::shared_ptr<UniformBuffer> createBindlessMeshDataBuffer();


private:
    uint32_t m_indexCount;
    std::shared_ptr<VertexBuffer> m_vertexBuffer;
    std::shared_ptr<IndexBuffer> m_indexBuffer;

    //std::shared_ptr<UniformBuffer> m_objectDataBuffer; // per mesh data
    //uint32_t m_bindlessMeshDataIndex;
    //static std::unique_ptr<DescriptorSubAllocationBase<Buffer>> s_bindlessMeshDataAllocation;

};



}