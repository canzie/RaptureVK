#include "TextureFlattener.h"

#include <cmath>

#include "AssetManager/AssetManager.h"
#include "Buffers/CommandBuffers/CommandPool.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Logging/Log.h"
#include "Shaders/ShaderCompilation.h"
#include "WindowContext/Application.h"

namespace Rapture {

#define FLATTENING_ENABLED 1

// Static member definitions
std::map<FlattenerDataType, std::shared_ptr<Shader>> TextureFlattener::s_flattenShaders;
std::shared_ptr<Shader> TextureFlattener::s_flattenDepthShader = nullptr;
std::map<FlattenerDataType, std::shared_ptr<ComputePipeline>> TextureFlattener::s_flattenPipelines;
std::shared_ptr<ComputePipeline> TextureFlattener::s_flattenDepthPipeline = nullptr;
bool TextureFlattener::s_initialized = false;

// FlattenTexture implementation
FlattenTexture::FlattenTexture(std::shared_ptr<Texture> inputTexture, std::shared_ptr<Texture> flattenedTexture,
                               const std::string &name, FlattenerDataType dataType)
    : m_inputTexture(inputTexture), m_flattenedTexture(flattenedTexture), m_dataType(dataType), m_name(name)
{

    // Add input texture to bindless texture array and get its index
    m_inputTextureBindlessIndex = inputTexture->getBindlessIndex();

    // Create a dedicated descriptor set for the output texture
    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding outputBinding = {};
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.location = DescriptorSetBindingLocation::CUSTOM_FLATTEN_OUTPUT;
    outputBinding.useStorageImageInfo = true;
    bindings.bindings.push_back(outputBinding);

    m_descriptorSet = std::make_shared<DescriptorSet>(bindings);

    // Update the descriptor set with the output texture
    m_descriptorSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_FLATTEN_OUTPUT)->add(m_flattenedTexture);
}

void FlattenTexture::update(std::shared_ptr<CommandBuffer> commandBuffer)
{
#if FLATTENING_ENABLED
    if (!m_inputTexture || !m_flattenedTexture) {
        RP_CORE_ERROR("FlattenTexture::update - Invalid textures");
        return;
    }

    if (!TextureFlattener::s_initialized) {
        RP_CORE_ERROR("FlattenTexture::update - TextureFlattener not initialized");
        return;
    }

    const auto &inputSpec = m_inputTexture->getSpecification();
    const auto &outputSpec = m_flattenedTexture->getSpecification();

    // Determine if this is a depth texture
    bool isDepthTexture = (inputSpec.format == TextureFormat::D32F || inputSpec.format == TextureFormat::D24S8);

    // Input layout transition
    VkImageAspectFlags inputAspectMask = isDepthTexture ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageMemoryBarrier inputBarrier{};
    bool needInputBarrier = !isDepthTexture;

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

    // Output layout transition
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

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, static_cast<uint32_t>(preBarriers.size()), preBarriers.data());

    // Bind the appropriate compute pipeline
    std::shared_ptr<ComputePipeline> pipeline;
    if (isDepthTexture) {
        pipeline = TextureFlattener::s_flattenDepthPipeline;
    } else {
        pipeline = TextureFlattener::s_flattenPipelines[m_dataType];
    }
    pipeline->bind(commandBuffer->getCommandBufferVk());

    // Bind descriptor sets
    DescriptorManager::bindSet(3, commandBuffer, pipeline);               // Bindless textures
    m_descriptorSet->bind(commandBuffer->getCommandBufferVk(), pipeline); // Custom output texture

    // Set push constants with bindless indices and texture dimensions
    TextureFlattener::FlattenPushConstants pushConstants;
    pushConstants.inputTextureIndex = m_inputTextureBindlessIndex;
    pushConstants.layerCount = inputSpec.depth;
    pushConstants.layerWidth = inputSpec.width;
    pushConstants.layerHeight = inputSpec.height;
    pushConstants.tilesPerRow = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(inputSpec.depth))));

    vkCmdPushConstants(commandBuffer->getCommandBufferVk(), pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(TextureFlattener::FlattenPushConstants), &pushConstants);

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

    vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &finalBarrier);
#endif
}

// TextureFlattener implementation
std::shared_ptr<FlattenTexture> TextureFlattener::createFlattenTexture(std::shared_ptr<Texture> inputTexture,
                                                                       const std::string &name, FlattenerDataType dataType)
{
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

    // Ensure the required shader and pipeline for the given data type are created
    getOrCreateShaderAndPipeline(dataType);

    // Create the flattened output texture
    auto flattenedTexture = createFlattenedTextureSpec(inputTexture);
    if (!flattenedTexture) {
        RP_CORE_ERROR("TextureFlattener::createFlattenTexture - Failed to create output texture");
        return nullptr;
    }

    // Create FlattenTexture instance
    auto flattenTexture = std::make_shared<FlattenTexture>(inputTexture, flattenedTexture, name, dataType);

    // Register the flattened texture with the asset manager
    AssetVariant flattenedVariant = flattenedTexture;
    std::shared_ptr<AssetVariant> flattenedVariantPtr = std::make_shared<AssetVariant>(flattenedVariant);
    AssetManager::registerVirtualAsset(flattenedVariantPtr, name, AssetType::Texture);

    // Mark as ready for sampling
    flattenedTexture->setReadyForSampling(true);

    RP_CORE_INFO("TextureFlattener: Successfully created flattened texture '{}' ({}x{}x{} -> {}x{})", name,
                 inputTexture->getSpecification().width, inputTexture->getSpecification().height,
                 inputTexture->getSpecification().depth, flattenedTexture->getSpecification().width,
                 flattenedTexture->getSpecification().height);

    return flattenTexture;
}

void TextureFlattener::initializeSharedResources()
{
    auto &app = Application::getInstance();
    auto &proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    // Load the depth texture flatten shader
    auto [flattenDepthShader, flattenDepthShaderHandle] =
        AssetManager::importAsset<Shader>(shaderDir / "glsl/FlattenDepthArray.cs.glsl");
    s_flattenDepthShader = flattenDepthShader;

    // Create compute pipelines
    ComputePipelineConfiguration flattenDepthConfig;
    flattenDepthConfig.shader = s_flattenDepthShader;
    s_flattenDepthPipeline = std::make_shared<ComputePipeline>(flattenDepthConfig);

    s_initialized = true;
    RP_CORE_INFO("TextureFlattener: Initialized shared resources (depth shader)");
}

void TextureFlattener::getOrCreateShaderAndPipeline(FlattenerDataType dataType)
{
    if (s_flattenShaders.find(dataType) != s_flattenShaders.end()) {
        return; // Already created
    }

    auto &app = Application::getInstance();
    auto &proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();
    auto shaderPath = shaderDir / "glsl/Flatten2dArray.cs.glsl";

    ShaderImportConfig importConfig;

    switch (dataType) {
    case FlattenerDataType::INT:
        importConfig.compileInfo.macros.push_back("DATA_TYPE_INT");
        break;
    case FlattenerDataType::UINT:
        importConfig.compileInfo.macros.push_back("DATA_TYPE_UINT");
        break;
    case FlattenerDataType::FLOAT:
    default:
        importConfig.compileInfo.macros.push_back("DATA_TYPE_FLOAT");
        break;
    }

    auto [shader, shaderHandle] = AssetManager::importAsset<Shader>(shaderPath, importConfig);
    s_flattenShaders[dataType] = shader;

    ComputePipelineConfiguration pipelineConfig;
    pipelineConfig.shader = shader;
    s_flattenPipelines[dataType] = std::make_shared<ComputePipeline>(pipelineConfig);

    RP_CORE_INFO("TextureFlattener: Created shader and pipeline for data type {}", (int)dataType);
}

std::shared_ptr<Texture> TextureFlattener::createFlattenedTextureSpec(std::shared_ptr<Texture> inputTexture)
{
    const auto &inputSpec = inputTexture->getSpecification();

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
    bool isDepthTexture = (inputSpec.format == TextureFormat::D32F || inputSpec.format == TextureFormat::D24S8);
    flattenedSpec.format = isDepthTexture ? TextureFormat::RGBA32F : inputSpec.format;

    if (inputSpec.format == TextureFormat::R8UI) {
        flattenedSpec.format = TextureFormat::RGBA32F;
    }

    flattenedSpec.filter = inputSpec.filter;
    flattenedSpec.storageImage = true;
    flattenedSpec.srgb = inputSpec.srgb;
    flattenedSpec.wrap = inputSpec.wrap;

    return std::make_shared<Texture>(flattenedSpec);
}

} // namespace Rapture