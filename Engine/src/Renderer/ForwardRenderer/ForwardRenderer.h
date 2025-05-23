#pragma once


#include "RenderTargets/FrameBuffers/Renderpass.h"
#include "Pipelines/GraphicsPipeline.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "RenderTargets/FrameBuffers/FrameBuffer.h"
#include "Shaders/Shader.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Materials/MaterialInstance.h"

#include <memory>
#include <vector>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace Rapture {


    struct UniformBufferObject {
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 proj = glm::mat4(1.0f);
    };

class ForwardRenderer {

    public:

        static void init();
        static void shutdown();

        static void drawFrame();

    private:
        static void setupSwapChain();
        // will be moved to material system
        static void setupShaders();

        static void setupRenderPass();
        static void setupGraphicsPipeline();
        static void setupFramebuffers();
        static void setupCommandPool();
        static void setupVertexBuffer();
        static void setupIndexBuffer();
        static void setupCommandBuffers();
        static void setupSyncObjects();

        static void cleanupSwapChain();

        static void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        static void recreateSwapChain();

        static void createUniformBuffers();
        static void updateUniformBuffers();

        static void createDescriptorPool();
        static void createDescriptorSets();

    private:
        static std::shared_ptr<Renderpass> m_renderPass;
        static std::shared_ptr<GraphicsPipeline> m_graphicsPipeline;
        static std::vector<std::shared_ptr<FrameBuffer>> m_framebuffers;
        static std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
        static std::shared_ptr<SwapChain> m_swapChain;
        static std::shared_ptr<CommandPool> m_commandPool;

        static std::shared_ptr<Shader> m_shader;

        static std::shared_ptr<VertexBuffer> m_vertexBuffer;
        static std::shared_ptr<IndexBuffer> m_indexBuffer;


        static std::vector<VkSemaphore> m_imageAvailableSemaphores;
        static std::vector<VkSemaphore> m_renderFinishedSemaphores;
        static std::vector<VkFence> m_inFlightFences;

        static VmaAllocator m_vmaAllocator;

        static VkDevice m_device;

        static float m_zoom;

        static bool m_framebufferResized;
        static uint32_t m_currentFrame;

        static VkQueue m_graphicsQueue;
        static VkQueue m_presentQueue;

        static std::shared_ptr<MaterialInstance> m_defaultMaterial;

        static std::vector<std::shared_ptr<UniformBuffer>> m_uniformBuffers;
        static std::vector<UniformBufferObject> m_ubos;

        static VkDescriptorPool m_descriptorPool;
        static std::vector<VkDescriptorSet> m_descriptorSets;

    };



}

