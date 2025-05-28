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
#include "Scenes/Scene.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"


#include <memory>
#include <vector>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "Meshes/Mesh.h"

namespace Rapture {

    // Forward declarations
    struct MeshComponent;
    struct TransformComponent;
    struct LightComponent;

    // Maximum number of lights supported
    static constexpr uint32_t MAX_LIGHTS = 16;

    // Camera/View uniform buffer object (binding 0)
    struct CameraUniformBufferObject {
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 proj = glm::mat4(1.0f);
    };

    // Light data structure for shader
    struct LightData {
        alignas(16) glm::vec4 position;      // w = light type (0 = point, 1 = directional, 2 = spot)
        alignas(16) glm::vec4 direction;     // w = range
        alignas(16) glm::vec4 color;         // w = intensity
        alignas(16) glm::vec4 spotAngles;    // x = inner cone cos, y = outer cone cos, z = unused, w = unused
    };

    // Light uniform buffer object (binding 1)
    struct LightUniformBufferObject {
        alignas(4) uint32_t numLights = 0;
        LightData lights[MAX_LIGHTS];
    };

    struct PushConstants {
        glm::mat4 model = glm::mat4(1.0f);
        glm::vec3 camPos = glm::vec3(0.0f);
    };

class ForwardRenderer {

    public:

        static void init();
        static void shutdown();

        static void drawFrame(std::shared_ptr<Scene> activeScene);
        static std::shared_ptr<Renderpass> getRenderpass() { return m_renderPass; }

    private:
        // will be moved to material system
        static void setupShaders();

        static void setupRenderPass();
        static void setupGraphicsPipeline();
        static void setupFramebuffers();
        static void setupCommandPool();
        static void setupCommandBuffers();
        static void setupSyncObjects();

        static void cleanupSwapChain();

        static void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, std::shared_ptr<Scene> activeScene);

        static void recreateSwapChain();

        static void createUniformBuffers();
        static void updateUniformBuffers();
        static void setupLights(std::shared_ptr<Scene> activeScene);
        static void updateLights(std::shared_ptr<Scene> activeScene);

        static void createDescriptorPool();
        static void createDescriptorSets();


    private:
        static std::shared_ptr<Renderpass> m_renderPass;
        static std::shared_ptr<GraphicsPipeline> m_graphicsPipeline;
        static std::vector<std::shared_ptr<FrameBuffer>> m_framebuffers;
        static std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
        static std::shared_ptr<CommandPool> m_commandPool;

        static std::shared_ptr<Shader> m_shader;


        static std::shared_ptr<SwapChain> m_swapChain;

        static std::vector<VkSemaphore> m_imageAvailableSemaphores;
        static std::vector<VkSemaphore> m_renderFinishedSemaphores;
        static std::vector<VkFence> m_inFlightFences;

        static VmaAllocator m_vmaAllocator;

        static VkDevice m_device;

        static float m_zoom;

        static bool m_framebufferResized;
        static uint32_t m_currentFrame;

        static std::shared_ptr<VulkanQueue> m_graphicsQueue;
        static std::shared_ptr<VulkanQueue> m_presentQueue;


        // Camera uniform buffers (binding 0)
        static std::vector<std::shared_ptr<UniformBuffer>> m_cameraUniformBuffers;
        static std::vector<CameraUniformBufferObject> m_cameraUbos;
        
        // Light uniform buffers (binding 1)
        static std::vector<std::shared_ptr<UniformBuffer>> m_lightUniformBuffers;
        static std::vector<LightUniformBufferObject> m_lightUbos;
        
        // Light management
        static bool m_lightsNeedUpdate;

        static VkDescriptorPool m_descriptorPool;
        static std::vector<VkDescriptorSet> m_descriptorSets;

    };



}

