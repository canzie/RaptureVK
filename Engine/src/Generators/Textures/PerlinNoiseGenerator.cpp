#include "PerlinNoiseGenerator.h"
#include "WindowContext/Application.h"
#include "Pipelines/ComputePipeline.h"
#include "Shaders/Shader.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/CommandBuffers/CommandBuffer.h"
#include "Logging/Log.h"
#include "AssetManager/AssetManager.h"

#include <random>

namespace Rapture {

struct PerlinNoisePushConstants {
    int octaves;
    float persistence;
    float lacunarity;
    float scale;
    uint32_t seed;
};

// Static member definitions
std::unique_ptr<ComputePipeline> PerlinNoiseGenerator::m_computePipeline = nullptr;
std::shared_ptr<Shader> PerlinNoiseGenerator::m_computeShader = nullptr;

std::shared_ptr<Texture> PerlinNoiseGenerator::generateNoise(int width, int height, int octaves, float persistence, float lacunarity, float scale)
{
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    
    RP_CORE_INFO("Generating Perlin noise texture: {}x{} with {} octaves", width, height, octaves);
    
    std::shared_ptr<Texture> outputTexture = nullptr;

    try {
        // Initialize compute resources if needed
        initializeComputeResources();
        
        // Create output texture
        outputTexture = createOutputTexture(width, height);
        
        if (outputTexture == nullptr) {
            RP_CORE_ERROR("Failed to create output texture for Perlin noise generation!");
            throw std::runtime_error("Failed to create output texture for Perlin noise generation!");
        }

        // Create descriptor set for storage image
        auto descriptorSet = createStorageImageDescriptorSet(outputTexture);
        
        // Get compute queue
        auto computeQueue = vulkanContext.getComputeQueue();
        
        // Create command pool for compute operations
        CommandPoolConfig poolConfig{};
        poolConfig.queueFamilyIndex = vulkanContext.getQueueFamilyIndices().computeFamily.value();
        poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolConfig.threadId = 0;
        
        auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
        auto commandBuffer = commandPool->getCommandBuffer();
        
        // Begin command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);
        
        // Transition texture to general layout for compute shader access
        transitionImageLayoutForCompute(commandBuffer->getCommandBufferVk(), outputTexture->getImage());
        
        // Bind compute pipeline
        m_computePipeline->bind(commandBuffer->getCommandBufferVk());
        
        // Bind descriptor set
        VkDescriptorSet descriptorSetVk = descriptorSet->getDescriptorSet();
        vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(),
                               VK_PIPELINE_BIND_POINT_COMPUTE,
                               m_computePipeline->getPipelineLayoutVk(),
                               0, 1, &descriptorSetVk, 0, nullptr);
        
        // Dispatch compute shader with push constants
        dispatchComputeShader(commandBuffer->getCommandBufferVk(), width, height, octaves, persistence, lacunarity, scale);
        
        // Transition texture to shader read only optimal for sampling
        transitionImageLayoutForSampling(commandBuffer->getCommandBufferVk(), outputTexture->getImage());
        
        // End command buffer
        if (vkEndCommandBuffer(commandBuffer->getCommandBufferVk()) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to record command buffer for Perlin noise generation!");
            throw std::runtime_error("Failed to record command buffer!");
        }
        
        // Submit command buffer
        VkCommandBuffer cmdBuffer = commandBuffer->getCommandBufferVk();

        
        VkDevice device = vulkanContext.getLogicalDevice();
        VkFence fence;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        
        if (vkCreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            RP_CORE_ERROR("Failed to create fence for Perlin noise generation!");
            throw std::runtime_error("Failed to create fence!");
        }
        
        computeQueue->submitQueue(commandBuffer, fence);
        
        // Wait for completion
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr);
        
        // Mark texture as ready for sampling
        outputTexture->setReadyForSampling(true);
        
        RP_CORE_INFO("Successfully generated Perlin noise texture");
        
    } catch (const std::exception& e) {
        RP_CORE_ERROR("Failed to generate Perlin noise: {}", e.what());
        throw;
    }


    AssetVariant variant = outputTexture;
    std::shared_ptr<AssetVariant> variantPtr = std::make_shared<AssetVariant>(variant);

    AssetManager::registerVirtualAsset(variantPtr, "PerlinNoiseTexture", AssetType::Texture);
    
    return outputTexture;
}

void PerlinNoiseGenerator::initializeComputeResources()
{
    if (!m_computePipeline || !m_computeShader) {

        auto& app = Application::getInstance();
        auto projectShaderDirectory = app.getProject().getProjectShaderDirectory();
        // Load compute shader
        auto [computeShader, computeShaderHandle] = AssetManager::importAsset<Shader>(projectShaderDirectory / "SPIRV/Generators/PerlinNoise.cs.spv");
        
        // Create compute pipeline
        ComputePipelineConfiguration pipelineConfig{};
        pipelineConfig.shader = computeShader;
        m_computePipeline = std::make_unique<ComputePipeline>(pipelineConfig);
        m_computeShader = computeShader;
        
        RP_CORE_INFO("Initialized Perlin noise compute resources");
    }
}

std::shared_ptr<Texture> PerlinNoiseGenerator::createOutputTexture(int width, int height)
{
    // Create texture specification for storage image
    TextureSpecification spec{};
    spec.width = static_cast<uint32_t>(width);
    spec.height = static_cast<uint32_t>(height);
    spec.depth = 1;
    spec.type = TextureType::TEXTURE2D;
    spec.format = TextureFormat::RGBA8;
    spec.filter = TextureFilter::Linear;
    spec.wrap = TextureWrap::Repeat;
    spec.srgb = false; // Use linear for compute shader output
    spec.mipLevels = 1;
    spec.storageImage = true; // Enable storage image usage
    
    return std::make_shared<Texture>(spec);
}

std::unique_ptr<DescriptorSet> PerlinNoiseGenerator::createStorageImageDescriptorSet(std::shared_ptr<Texture> outputTexture)
{
    // Get descriptor set layout from compute shader
    const auto& descriptorSetLayouts = m_computeShader->getDescriptorSetLayouts();
    if (descriptorSetLayouts.empty()) {
        RP_CORE_ERROR("Compute shader has no descriptor set layouts!");
        throw std::runtime_error("Compute shader has no descriptor set layouts!");
    }
    
    // Create descriptor set binding for storage image
    DescriptorSetBinding binding{};
    binding.binding = 0;
    binding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.count = 1;
    binding.useStorageImageInfo = true; // Use storage image descriptor info
    binding.resource = outputTexture;
    
    // Create descriptor set bindings
    DescriptorSetBindings bindings{};
    bindings.bindings.push_back(binding);
    bindings.layout = descriptorSetLayouts[0];
    
    return std::make_unique<DescriptorSet>(bindings);
}

void PerlinNoiseGenerator::transitionImageLayoutForCompute(VkCommandBuffer commandBuffer, VkImage image)
{
    VkImageMemoryBarrier imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = image;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;
    imageBarrier.srcAccessMask = 0;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
}

void PerlinNoiseGenerator::transitionImageLayoutForSampling(VkCommandBuffer commandBuffer, VkImage image)
{
    VkImageMemoryBarrier imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = image;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;
    imageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
}

void PerlinNoiseGenerator::dispatchComputeShader(VkCommandBuffer commandBuffer, int width, int height, int octaves, float persistence, float lacunarity, float scale)
{
    // Set push constants
    std::random_device rd;
    std::mt19937 gen(rd());
    
    PerlinNoisePushConstants pushConstants{};
    pushConstants.octaves = octaves;
    pushConstants.persistence = persistence;
    pushConstants.lacunarity = lacunarity;
    pushConstants.scale = scale;
    pushConstants.seed = gen(); // Random seed
    
    vkCmdPushConstants(commandBuffer,
                      m_computePipeline->getPipelineLayoutVk(),
                      VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(PerlinNoisePushConstants), &pushConstants);
    
    // Dispatch compute shader (work group size is 8x8)
    uint32_t groupCountX = (width + 7) / 8;
    uint32_t groupCountY = (height + 7) / 8;
    
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
}

}