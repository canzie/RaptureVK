#include "Mesh.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"

namespace Rapture {

    //std::unique_ptr<DescriptorSubAllocationBase<Buffer>> Mesh::s_bindlessMeshDataAllocation = nullptr;

    Mesh::Mesh(AllocatorParams& params){
        setMeshData(params);
    }

    Mesh::Mesh()
        : m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_indexCount(0) {
    }

    Mesh::~Mesh() {
        //if (m_bindlessMeshDataIndex != UINT32_MAX && s_bindlessMeshDataAllocation != nullptr) {
        //    s_bindlessMeshDataAllocation->free(m_bindlessMeshDataIndex);
        //}
    }

    void Mesh::setMeshData(AllocatorParams& params)
    {
        auto& app = Application::getInstance();
        auto& vulkanContext = app.getVulkanContext();

        m_indexCount = params.indexCount;

        m_vertexBuffer = std::make_shared<VertexBuffer>(params.vertexDataSize, BufferUsage::STATIC, vulkanContext.getVmaAllocator());

        m_indexBuffer = std::make_shared<IndexBuffer>(params.indexDataSize, BufferUsage::STATIC, vulkanContext.getVmaAllocator(), params.indexType);

        if (params.bufferLayout.calculateVertexSize() != 0) {
            m_vertexBuffer->setBufferLayout(params.bufferLayout);
        }

        m_vertexBuffer->addDataGPU(params.vertexData, params.vertexDataSize, 0);
        m_indexBuffer->addDataGPU(params.indexData, params.indexDataSize, 0);


        auto& bufferPoolManager = BufferPoolManager::getInstance();

        BufferAllocationRequest vertexRequest;
        vertexRequest.size = params.vertexDataSize;
        vertexRequest.type = BufferType::VERTEX;
        vertexRequest.usage = BufferUsage::STATIC;
        vertexRequest.layout = params.bufferLayout;
        vertexRequest.indexSize = params.indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
        vertexRequest.alignment = params.bufferLayout.calculateVertexSize();
        m_vertexAllocation = bufferPoolManager.allocateBuffer(vertexRequest);


        BufferAllocationRequest indexRequest;
        indexRequest.size = params.indexDataSize;
        indexRequest.type = BufferType::INDEX;
        indexRequest.usage = BufferUsage::STATIC;
        indexRequest.layout = params.bufferLayout;
        indexRequest.indexSize = params.indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
        indexRequest.alignment = params.bufferLayout.calculateVertexSize();

        m_indexAllocation = bufferPoolManager.allocateBuffer(indexRequest);

        // Upload data to the BufferPool allocations
        if (m_vertexAllocation && params.vertexData) {
            m_vertexAllocation->uploadData(params.vertexData, params.vertexDataSize);

        }
        
        if (m_indexAllocation && params.indexData) {
            m_indexAllocation->uploadData(params.indexData, params.indexDataSize);

        }

    }

}