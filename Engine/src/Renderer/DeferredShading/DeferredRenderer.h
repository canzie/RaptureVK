#pragma once


#include "Pipelines/GraphicsPipeline.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "Shaders/Shader.h"
#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"
#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Materials/MaterialInstance.h"
#include "Scenes/Scene.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"
#include "Cameras/CameraCommon.h"
#include "Components/Components.h"
#include "Renderer/DeferredShading/GBufferPass.h"

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




class DeferredRenderer {

    public:

        static void init();
        static void shutdown();

        static void drawFrame(std::shared_ptr<Scene> activeScene);

        static void onSwapChainRecreated();

        // Getter for GBuffer pass
        static std::shared_ptr<GBufferPass> getGBufferPass() { return m_gbufferPass; }


    private:

        // sets up command pools and buffers
        static void setupCommandResources();

        static void recordCommandBuffer(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Scene> activeScene, uint32_t imageIndex);



    private:
        static std::shared_ptr<GBufferPass> m_gbufferPass;
        static std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
        static std::shared_ptr<CommandPool> m_commandPool;

        static std::shared_ptr<Shader> m_shader;


        static VmaAllocator m_vmaAllocator;
        static VkDevice m_device;
        static std::shared_ptr<SwapChain> m_swapChain;


        static uint32_t m_currentFrame;

        static std::shared_ptr<VulkanQueue> m_graphicsQueue;
        static std::shared_ptr<VulkanQueue> m_presentQueue;

        

    };



}

