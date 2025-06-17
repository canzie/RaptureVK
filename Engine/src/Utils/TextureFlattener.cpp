#include "TextureFlattener.h"

#include <cmath>

#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Logging/Log.h"
#include "WindowContext/Application.h"

namespace Rapture {

// Static member definitions
std::shared_ptr<Shader> TextureFlattener::s_flattenShader = nullptr;
std::shared_ptr<Shader> TextureFlattener::s_flattenDepthShader = nullptr;
std::shared_ptr<ComputePipeline> TextureFlattener::s_flattenPipeline = nullptr;
std::shared_ptr<ComputePipeline> TextureFlattener::s_flattenDepthPipeline = nullptr;
bool TextureFlattener::s_initialized = false;



// FlattenTexture implementation
FlattenTexture::FlattenTexture(std::shared_ptr<Texture> inputTexture, std::shared_ptr<Texture> flattenedTexture, const std::string& name)
    : m_inputTexture(inputTexture), m_flattenedTexture(flattenedTexture), m_name(name) {
    
    // Create descriptor set for this texture combination
    m_descriptorSet = TextureFlattener::createDescriptorSet(inputTexture, flattenedTexture);
}

void FlattenTexture::update(std::shared_ptr<CommandBuffer> commandBuffer) {
    if (!m_inputTexture || !m_flattenedTexture || !m_descriptorSet) {
        RP_CORE_ERROR("FlattenTexture::update - Invalid textures or descriptor set");
        return;
    }

    if (!TextureFlattener::s_initialized) {
        RP_CORE_ERROR("FlattenTexture::update - TextureFlattener not initialized");
        return;
    }

    const auto& inputSpec = m_inputTexture->getSpecification();
    const auto& outputSpec = m_flattenedTexture->getSpecification();
    
    // Determine if this is a depth texture
    bool isDepthTexture = (inputSpec.format == TextureFormat::D32F || 
                          inputSpec.format == TextureFormat::D24S8);
    // Determine the correct aspect mask based on the input texture format
    VkImageAspectFlags inputAspectMask = isDepthTexture ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    
    // For depth textures, they're typically already in shader read optimal layout when the flattener runs
    // We'll skip the input barrier for depth textures since they're already in the correct layout
    VkImageMemoryBarrier inputBarrier{};
    bool needInputBarrier = !isDepthTexture; // Only barrier for non-depth textures
    
    if (needInputBarrier) {
        inputBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        inputBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        inputBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        inputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        inputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        inputBarrier.image = m_inputTexture->getImage();
        inputBarrier.subresourceRange.aspectMask = inputAspectMask;
        inputBarrier.subresourceRange.baseMipLevel = 0;
        inputBarrier.subresourceRange.levelCount = 1;
        inputBarrier.subresourceRange.baseArrayLayer = 0;
        inputBarrier.subresourceRange.layerCount = inputSpec.depth;
        inputBarrier.srcAccessMask = 0;
        inputBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    
    // Transition output texture to general layout for storage image access
    VkImageMemoryBarrier outputBarrier{};
    outputBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outputBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    outputBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outputBarrier.image = m_flattenedTexture->getImage();
    outputBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outputBarrier.subresourceRange.baseMipLevel = 0;
    outputBarrier.subresourceRange.levelCount = 1;
    outputBarrier.subresourceRange.baseArrayLayer = 0;
    outputBarrier.subresourceRange.layerCount = 1;
    outputBarrier.srcAccessMask = 0;
    outputBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    
    std::vector<VkImageMemoryBarrier> preBarriers;
    if (needInputBarrier) {
        preBarriers.push_back(inputBarrier);
    }
    preBarriers.push_back(outputBarrier);
    
    VkPipelineStageFlags srcStage = isDepthTexture ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        srcStage,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(preBarriers.size()), preBarriers.data()
    );
    
    // Bind the appropriate compute pipeline based on texture type
    auto pipeline = isDepthTexture ? TextureFlattener::s_flattenDepthPipeline : TextureFlattener::s_flattenPipeline;
    pipeline->bind(commandBuffer->getCommandBufferVk());
    
    // Set push constants
    TextureFlattener::FlattenPushConstants pushConstants;
    pushConstants.layerCount = inputSpec.depth;
    pushConstants.layerWidth = inputSpec.width;
    pushConstants.layerHeight = inputSpec.height;
    pushConstants.tilesPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(inputSpec.depth))));
    
    vkCmdPushConstants(commandBuffer->getCommandBufferVk(),
                       pipeline->getPipelineLayoutVk(),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(TextureFlattener::FlattenPushConstants), &pushConstants);
    
    // Bind descriptor set
    VkDescriptorSet descriptorSetVk = m_descriptorSet->getDescriptorSet();
    vkCmdBindDescriptorSets(commandBuffer->getCommandBufferVk(),
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline->getPipelineLayoutVk(),
                            0, 1, &descriptorSetVk, 0, nullptr);
    
    // Dispatch compute shader
    uint32_t workGroupsX = (outputSpec.width + 15) / 16;
    uint32_t workGroupsY = (outputSpec.height + 15) / 16;
    vkCmdDispatch(commandBuffer->getCommandBufferVk(), workGroupsX, workGroupsY, 1);
    
    // Transition output texture to shader read optimal layout
    VkImageMemoryBarrier finalBarrier{};
    finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    finalBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    finalBarrier.image = m_flattenedTexture->getImage();
    finalBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    finalBarrier.subresourceRange.baseMipLevel = 0;
    finalBarrier.subresourceRange.levelCount = 1;
    finalBarrier.subresourceRange.baseArrayLayer = 0;
    finalBarrier.subresourceRange.layerCount = 1;
    finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer->getCommandBufferVk(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &finalBarrier
    );
}

// TextureFlattener implementation
std::shared_ptr<FlattenTexture> TextureFlattener::createFlattenTexture(std::shared_ptr<Texture> inputTexture, const std::string& name) {
    if (!inputTexture) {
        RP_CORE_ERROR("TextureFlattener::createFlattenTexture - Input texture is null");
        return nullptr;
    }
    
    if (inputTexture->getSpecification().type != TextureType::TEXTURE2D_ARRAY) {
        RP_CORE_ERROR("TextureFlattener::createFlattenTexture - Input texture must be a 2D array");
        return nullptr;
    }

    // Initialize shared resources if needed
    if (!s_initialized) {
        initializeSharedResources();
    }

    // Create the flattened output texture
    auto flattenedTexture = createFlattenedTextureSpec(inputTexture);
    if (!flattenedTexture) {
        RP_CORE_ERROR("TextureFlattener::createFlattenTexture - Failed to create output texture");
        return nullptr;
    }

    // Create FlattenTexture instance
    auto flattenTexture = std::make_shared<FlattenTexture>(inputTexture, flattenedTexture, name);

    // Register the flattened texture with the asset manager
    AssetVariant flattenedVariant = flattenedTexture;
    std::shared_ptr<AssetVariant> flattenedVariantPtr = std::make_shared<AssetVariant>(flattenedVariant);
    AssetManager::registerVirtualAsset(flattenedVariantPtr, name, AssetType::Texture);
    
    // Mark as ready for sampling
    flattenedTexture->setReadyForSampling(true);
    
    RP_CORE_INFO("TextureFlattener: Successfully created flattened texture '{}' ({}x{}x{} -> {}x{})", 
                 name,
                 inputTexture->getSpecification().width, 
                 inputTexture->getSpecification().height,
                 inputTexture->getSpecification().depth,
                 flattenedTexture->getSpecification().width,
                 flattenedTexture->getSpecification().height);
    
    return flattenTexture;
}

void TextureFlattener::initializeSharedResources() {
    auto& app = Application::getInstance();
    auto& proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();
    
    // Load the color texture flatten shader
    auto [flattenShader, flattenShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/Flatten2dArray.cs.glsl");
    s_flattenShader = flattenShader;
    
    // Load the depth texture flatten shader
    auto [flattenDepthShader, flattenDepthShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/FlattenDepthArray.cs.glsl");
    s_flattenDepthShader = flattenDepthShader;
    
    // Create compute pipelines
    ComputePipelineConfiguration flattenConfig;
    flattenConfig.shader = s_flattenShader;
    s_flattenPipeline = std::make_shared<ComputePipeline>(flattenConfig);
    
    ComputePipelineConfiguration flattenDepthConfig;
    flattenDepthConfig.shader = s_flattenDepthShader;
    s_flattenDepthPipeline = std::make_shared<ComputePipeline>(flattenDepthConfig);
    
    s_initialized = true;
    RP_CORE_INFO("TextureFlattener: Initialized shared resources (color and depth shaders)");
}

std::shared_ptr<Texture> TextureFlattener::createFlattenedTextureSpec(std::shared_ptr<Texture> inputTexture) {
    const auto& inputSpec = inputTexture->getSpecification();
    
    // Calculate grid layout for flattening
    uint32_t tilesPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(inputSpec.depth))));
    uint32_t tilesPerCol = static_cast<uint32_t>(std::ceil(static_cast<float>(inputSpec.depth) / tilesPerRow));
    
    // Create specification for flattened texture
    TextureSpecification flattenedSpec;
    flattenedSpec.width = inputSpec.width * tilesPerRow;
    flattenedSpec.height = inputSpec.height * tilesPerCol;
    flattenedSpec.depth = 1;
    flattenedSpec.type = TextureType::TEXTURE2D;
    
    // For depth textures, output should be a color format for visualization
    // For color textures, keep the same format
    bool isDepthTexture = (inputSpec.format == TextureFormat::D32F || 
                          inputSpec.format == TextureFormat::D24S8);
    flattenedSpec.format = isDepthTexture ? TextureFormat::RGBA32F : inputSpec.format;
    
    flattenedSpec.filter = inputSpec.filter;
    flattenedSpec.storageImage = true;
    flattenedSpec.srgb = inputSpec.srgb;
    flattenedSpec.wrap = inputSpec.wrap;
    
    return std::make_shared<Texture>(flattenedSpec);
}

std::shared_ptr<DescriptorSet> TextureFlattener::createDescriptorSet(std::shared_ptr<Texture> inputTexture, std::shared_ptr<Texture> outputTexture) {
    // Determine if this is a depth texture
    bool isDepthTexture = (inputTexture->getSpecification().format == TextureFormat::D32F || 
                          inputTexture->getSpecification().format == TextureFormat::D24S8);
    
    // Choose the appropriate shader
    auto shader = isDepthTexture ? s_flattenDepthShader : s_flattenShader;
    
    if (!shader || shader->getDescriptorSetLayouts().empty()) {
        RP_CORE_ERROR("TextureFlattener::createDescriptorSet - Invalid shader or descriptor set layouts");
        return nullptr;
    }
    
    DescriptorSetBindings bindings;
    bindings.layout = shader->getDescriptorSetLayouts()[0];
    
    // Binding 0: Input texture array (sampler2DArray)
    DescriptorSetBinding inputBinding;
    inputBinding.binding = 0;
    inputBinding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    inputBinding.count = 1;
    inputBinding.viewType = TextureViewType::DEFAULT;
    inputBinding.resource = inputTexture;
    inputBinding.useStorageImageInfo = false;
    bindings.bindings.push_back(inputBinding);
    
    // Binding 1: Output flattened texture (storage image2D)
    DescriptorSetBinding outputBinding;
    outputBinding.binding = 1;
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.count = 1;
    outputBinding.viewType = TextureViewType::DEFAULT;
    outputBinding.resource = outputTexture;
    outputBinding.useStorageImageInfo = true;
    bindings.bindings.push_back(outputBinding);
    
    return std::make_shared<DescriptorSet>(bindings);
}

} 