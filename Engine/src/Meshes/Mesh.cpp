#include "Mesh.h"
#include "WindowContext/Application.h"

#include "Logging/Log.h"

namespace Rapture {

    Mesh::Mesh(AllocatorParams& params)
    {
        setMeshData(params);
    }

    Mesh::Mesh()
        : m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_indexCount(0)
    {
    }

    Mesh::~Mesh()
    {
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

    }

}