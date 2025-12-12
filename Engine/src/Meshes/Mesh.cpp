#include "Mesh.h"
#include "Buffers/BufferPool.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"

namespace Rapture {

// std::unique_ptr<DescriptorSubAllocationBase<Buffer>> Mesh::s_bindlessMeshDataAllocation = nullptr;

Mesh::Mesh(AllocatorParams &params)
{
    setMeshData(params);
}

Mesh::Mesh() : m_indexCount(0), m_vertexBuffer(nullptr), m_indexBuffer(nullptr) {}

Mesh::~Mesh()
{
    m_indexAllocation.reset();
    m_vertexAllocation.reset();
}

void Mesh::setMeshData(AllocatorParams &params)
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    m_indexCount = params.indexCount;

    BufferAllocationRequest vertexRequest;
    vertexRequest.size = params.vertexDataSize;
    vertexRequest.usage = BufferUsage::STATIC;
    vertexRequest.layout = params.bufferLayout;
    vertexRequest.indexSize = params.indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
    vertexRequest.alignment = params.bufferLayout.calculateVertexSize();

    BufferAllocationRequest indexRequest;
    indexRequest.size = params.indexDataSize;
    indexRequest.usage = BufferUsage::STATIC;
    indexRequest.layout = params.bufferLayout;
    indexRequest.indexSize = params.indexType == VK_INDEX_TYPE_UINT32 ? 4 : 2;
    indexRequest.alignment = params.bufferLayout.calculateVertexSize();

    m_vertexBuffer = std::make_shared<VertexBuffer>(vertexRequest, vulkanContext.getVmaAllocator(), params.vertexData);
    m_indexBuffer = std::make_shared<IndexBuffer>(indexRequest, vulkanContext.getVmaAllocator(), params.indexData);

    m_indexAllocation = m_indexBuffer->getBufferAllocation();
    m_vertexAllocation = m_vertexBuffer->getBufferAllocation();

    if (!m_indexAllocation || !m_vertexAllocation) {
        RP_CORE_ERROR("Failed to create vertex or index buffer!");
        return;
    }
}

} // namespace Rapture