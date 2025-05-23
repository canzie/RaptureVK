#include "Mesh.h"
#include "WindowContext/Application.h"

namespace Rapture {
    Mesh::~Mesh()
    {
    }

    void Mesh::setMeshData(const AllocatorParams &params)
    {
        auto& app = Application::getInstance();
        auto& vulkanContext = app.getVulkanContext();

        m_vertexBuffer = std::make_shared<VertexBuffer>(params.vertexDataSize, BufferUsage::STATIC, vulkanContext.getVmaAllocator());
        m_indexBuffer = std::make_shared<IndexBuffer>(params.indexDataSize, BufferUsage::STATIC, vulkanContext.getVmaAllocator());
        
    }

}