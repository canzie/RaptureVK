#include "ProceduralTextures.h"

#include "AssetManager/Asset.h"
#include "AssetManager/AssetManager.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Logging/Log.h"
#include "Textures/Texture.h"
#include "WindowContext/Application.h"
#include "WindowContext/VulkanContext/VulkanQueue.h"

namespace Rapture {

ProceduralTexture::ProceduralTexture(const std::string &shaderPath, const ProceduralTextureConfig &config) : m_config(config)
{
    initFromShaderPath(shaderPath);
}

ProceduralTexture::ProceduralTexture(const AssetHandle &shaderHandle, const ProceduralTextureConfig &config) : m_config(config)
{
    initFromShaderHandle(shaderHandle);
}

ProceduralTexture::ProceduralTexture(const std::string &shaderPath, Texture &outputTexture) : m_texture(&outputTexture)
{
    initFromShaderPath(shaderPath, false);
}

ProceduralTexture::~ProceduralTexture() {}

void ProceduralTexture::initFromShaderPath(const std::string &shaderPath, bool createTexture)
{
    auto &app = Application::getInstance();
    auto &proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    auto asset = AssetManager::importAsset(shaderDir / shaderPath);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (!m_shader || !m_shader->isReady()) {
        RP_CORE_ERROR("Failed to load procedural texture shader: {}", shaderPath);
        return;
    }

    m_assets.push_back(std::move(asset));
    extractExpectedPushConstantSize();
    initPipeline();
    initCommandBuffer();

    if (createTexture) {
        initTexture();
    }

    initDescriptorSet();
    m_isValid = true;
}

void ProceduralTexture::initFromShaderHandle(const AssetHandle &shaderHandle, bool createTexture)
{
    auto asset = AssetManager::getAsset(shaderHandle);
    m_shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
    if (!m_shader || !m_shader->isReady()) {
        RP_CORE_ERROR("Failed to get ready shader from asset handle");
        return;
    }

    extractExpectedPushConstantSize();
    initPipeline();
    initCommandBuffer();

    if (createTexture) {
        initTexture();
    }

    initDescriptorSet();
    m_isValid = true;
}

void ProceduralTexture::initPipeline()
{
    ComputePipelineConfiguration config;
    config.shader = m_shader;
    m_pipeline = std::make_shared<ComputePipeline>(config);
}

void ProceduralTexture::initCommandBuffer()
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = vulkanContext.getComputeQueueIndex();
    poolConfig.flags = 0;

    m_commandPoolHash = CommandPoolManager::createCommandPool(poolConfig);
}

void ProceduralTexture::initTexture()
{
    TextureSpecification spec;
    spec.width = TEXTURE_SIZE;
    spec.height = TEXTURE_SIZE;
    spec.depth = 1;
    spec.type = TextureType::TEXTURE2D;
    spec.format = m_config.format;
    spec.filter = m_config.filter;
    spec.wrap = m_config.wrap;
    spec.srgb = m_config.srgb;
    spec.storageImage = true;
    spec.mipLevels = 1;

    auto texture = std::make_unique<Texture>(spec);

    std::string textureName = m_config.name;
    if (textureName.empty()) {
        static uint32_t s_proceduralTextureCounter = 0;
        textureName = "procedural_texture_" + std::to_string(s_proceduralTextureCounter++);
    }

    auto asset = AssetManager::registerVirtualAsset(std::move(texture), textureName, AssetType::TEXTURE);
    m_texture = asset ? asset.get()->getUnderlyingAsset<Texture>() : nullptr;
    m_assets.push_back(asset);
}

void ProceduralTexture::initDescriptorSet()
{
    if (!m_texture) {
        return;
    }

    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding outputBinding = {};
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
    outputBinding.useStorageImageInfo = true;
    bindings.bindings.push_back(outputBinding);

    m_descriptorSet = std::make_shared<DescriptorSet>(bindings);
    m_descriptorSet->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(*m_texture);
}

void ProceduralTexture::extractExpectedPushConstantSize()
{
    const auto &pushConstantLayouts = m_shader->getPushConstantLayouts();

    m_expectedPushConstantSize = 0;
    for (const auto &layout : pushConstantLayouts) {
        m_expectedPushConstantSize = std::max(m_expectedPushConstantSize, static_cast<size_t>(layout.offset + layout.size));
    }
}

bool ProceduralTexture::verifyPushConstantSize(size_t providedSize)
{
    if (providedSize != m_expectedPushConstantSize) {
        RP_CORE_ERROR("Push constant size mismatch: provided {} bytes, shader expects {} bytes", providedSize,
                      m_expectedPushConstantSize);
        return false;
    }
    return true;
}

void ProceduralTexture::generate()
{
    if (!m_isValid) {
        RP_CORE_ERROR("Cannot generate: ProceduralTexture is not valid");
        return;
    }

    if (m_expectedPushConstantSize > 0 && m_pushConstantData.empty()) {
        RP_CORE_ERROR("Cannot generate: push constants required but not set");
        return;
    }

    auto pool = CommandPoolManager::getCommandPool(m_commandPoolHash);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkCommandBuffer vkCmd = commandBuffer->getCommandBufferVk();

    VkImageMemoryBarrier preBarrier{};
    preBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    preBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    preBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.image = m_texture->getImage();
    preBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    preBarrier.subresourceRange.baseMipLevel = 0;
    preBarrier.subresourceRange.levelCount = 1;
    preBarrier.subresourceRange.baseArrayLayer = 0;
    preBarrier.subresourceRange.layerCount = 1;
    preBarrier.srcAccessMask = 0;
    preBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &preBarrier);

    m_pipeline->bind(vkCmd);
    m_descriptorSet->bind(vkCmd, m_pipeline);

    if (!m_pushConstantData.empty()) {
        vkCmdPushConstants(vkCmd, m_pipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           static_cast<uint32_t>(m_pushConstantData.size()), m_pushConstantData.data());
    }

    uint32_t workGroupsX = (TEXTURE_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    uint32_t workGroupsY = (TEXTURE_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(vkCmd, workGroupsX, workGroupsY, 1);

    VkImageMemoryBarrier postBarrier{};
    postBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    postBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    postBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    postBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.image = m_texture->getImage();
    postBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    postBarrier.subresourceRange.baseMipLevel = 0;
    postBarrier.subresourceRange.levelCount = 1;
    postBarrier.subresourceRange.baseArrayLayer = 0;
    postBarrier.subresourceRange.layerCount = 1;
    postBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    postBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &postBarrier);

    commandBuffer->end();

    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();
    auto queue = vulkanContext.getComputeQueue();

    queue->submitQueue(commandBuffer, nullptr, nullptr, VK_NULL_HANDLE);
    queue->waitIdle();
}

Texture *ProceduralTexture::generateWhiteNoise(uint32_t seed, const ProceduralTextureConfig &config)
{
    // Function-local static for shader handle - AssetManager handles caching
    static AssetHandle s_shaderHandle = 0;

    if (s_shaderHandle == 0) {
        auto &app = Application::getInstance();
        auto &proj = app.getProject();
        auto shaderDir = proj.getProjectShaderDirectory();

        auto asset = AssetManager::importAsset(shaderDir / "glsl/Generators/WhiteNoise.cs.glsl");
        auto shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
        if (!shader) {
            RP_CORE_ERROR("Failed to load WhiteNoise shader");
            return nullptr;
        }
        s_shaderHandle = asset.get()->getHandle();
    }

    ProceduralTexture generator(s_shaderHandle, config);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create white noise generator");
        return nullptr;
    }

    WhiteNoisePushConstants pc{};
    pc.seed = seed;
    generator.setPushConstants(pc);
    generator.generate();

    return &generator.getTexture();
}

Texture *ProceduralTexture::generatePerlinNoise(const PerlinNoisePushConstants &params, const ProceduralTextureConfig &config)
{
    static AssetHandle s_shaderHandle;

    if (0 == s_shaderHandle) {
        auto &app = Application::getInstance();
        auto &proj = app.getProject();
        auto shaderDir = proj.getProjectShaderDirectory();

        auto asset = AssetManager::importAsset(shaderDir / "glsl/Generators/PerlinNoise.cs.glsl");
        auto shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
        if (!shader) {
            RP_CORE_ERROR("Failed to load PerlinNoise shader");
            return nullptr;
        }
        s_shaderHandle = asset.get()->getHandle();
    }

    ProceduralTexture generator(s_shaderHandle, config);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create Perlin noise generator");
        return nullptr;
    }

    generator.setPushConstants(params);
    generator.generate();

    return &generator.getTexture();
}

Texture *ProceduralTexture::generateSimplexNoise(const SimplexNoisePushConstants &params, const ProceduralTextureConfig &config)
{
    static AssetHandle s_shaderHandle;

    if (s_shaderHandle == 0) {
        auto &app = Application::getInstance();
        auto &proj = app.getProject();
        auto shaderDir = proj.getProjectShaderDirectory();

        auto asset = AssetManager::importAsset(shaderDir / "glsl/Generators/SimplexNoise.cs.glsl");
        auto shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
        if (!shader) {
            RP_CORE_ERROR("Failed to load SimplexNoise shader");
            return nullptr;
        }
        s_shaderHandle = asset.get()->getHandle();
    }

    ProceduralTexture generator(s_shaderHandle, config);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create Simplex noise generator");
        return nullptr;
    }

    generator.setPushConstants(params);
    generator.generate();

    return &generator.getTexture();
}

Texture *ProceduralTexture::generateRidgedNoise(const RidgedNoisePushConstants &params, const ProceduralTextureConfig &config)
{
    static AssetHandle s_shaderHandle;

    if (s_shaderHandle == 0) {
        auto &app = Application::getInstance();
        auto &proj = app.getProject();
        auto shaderDir = proj.getProjectShaderDirectory();

        auto asset = AssetManager::importAsset(shaderDir / "glsl/Generators/RidgedNoise.cs.glsl");
        auto shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
        if (!shader) {
            RP_CORE_ERROR("Failed to load RidgedNoise shader");
            return nullptr;
        }
        s_shaderHandle = asset.get()->getHandle();
    }

    ProceduralTexture generator(s_shaderHandle, config);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create Ridged noise generator");
        return nullptr;
    }

    generator.setPushConstants(params);
    generator.generate();

    return &generator.getTexture();
}

Texture *ProceduralTexture::generateAtmosphere(float timeOfDay, const AtmospherePushConstants *params,
                                               const ProceduralTextureConfig &config)
{
    // Function-local static for shader handle - AssetManager handles caching
    static AssetHandle s_shaderHandle;

    if (s_shaderHandle == 0) {
        auto &app = Application::getInstance();
        auto &proj = app.getProject();
        auto shaderDir = proj.getProjectShaderDirectory();

        auto asset = AssetManager::importAsset(shaderDir / "glsl/Generators/Atmosphere.cs.glsl");
        auto shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
        if (!shader) {
            RP_CORE_ERROR("Failed to load Atmosphere shader");
            return nullptr;
        }
        s_shaderHandle = asset.get()->getHandle();
    }

    // Use HDR format by default for atmospheric scattering
    ProceduralTextureConfig atmosphereConfig = config;
    if (atmosphereConfig.format == TextureFormat::RGBA8) {
        atmosphereConfig.format = TextureFormat::RGBA16F;
    }

    ProceduralTexture generator(s_shaderHandle, atmosphereConfig);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create atmosphere generator");
        return nullptr;
    }

    (void)timeOfDay;

    // Set up push constants with defaults if not provided
    AtmospherePushConstants pc;
    if (params) {
        pc = *params;
    } else {
        // Earth-like atmospheric defaults
        pc.sunDir = glm::vec3(1.0f);
        pc.planetRadius = 6371e3f;
        pc.atmoRadius = 6471e3f;
        pc.betaRay = glm::vec3(5.5e-6, 13.0e-6, 22.4e-6);
        pc.scaleHeight = 8000.0f;
        pc.sunIntensity = 21e-6f;
    }

    generator.setPushConstants(pc);
    generator.generate();

    return &generator.getTexture();
}

} // namespace Rapture
