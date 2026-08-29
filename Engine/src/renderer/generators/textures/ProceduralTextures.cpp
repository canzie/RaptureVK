#include "ProceduralTextures.h"
#include "assets/textures/ATexture.h"

#include "core/utils/EnginePaths.h"

#include "scene/instances/Environment.h"

#include "app/Application.h"
#include "assets/asset_manager/Asset.h"
#include "assets/asset_manager/AssetImportConfig.h"
#include "assets/asset_manager/AssetManager.h"
#include "core/utils/Log.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "gpu/textures/Texture.h"
#include "gpu/vulkan_context/VulkanQueue.h"

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

ProceduralTexture::ProceduralTexture(const AssetHandle &shaderHandle, Texture &outputTexture) : m_texture(&outputTexture)
{
    initFromShaderHandle(shaderHandle, false);
}

ProceduralTexture::~ProceduralTexture() {}

void ProceduralTexture::initFromShaderPath(const std::string &shaderPath, bool createTexture)
{
    auto shaderDir = EnginePaths::shaderDirectory();

    auto asset = AssetManager::importAsset(shaderDir / shaderPath);
    m_shader = asset.as<AShader>();
    if (!m_shader || !m_shader->isReady()) {
        RP_CORE_ERROR("Failed to load procedural texture shader: {}", shaderPath);
        return;
    }

    extractExpectedPushConstantSize();
    initPipeline();
    initCommandBuffer();

    if (createTexture) {
        initTexture();
    }

    initDescriptorSet();
    reflectParameters();
    m_isValid = true;
}

void ProceduralTexture::initFromShaderHandle(const AssetHandle &shaderHandle, bool createTexture)
{
    auto asset = AssetManager::getAsset(shaderHandle);
    m_shader = asset.as<AShader>();
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
    reflectParameters();
    m_isValid = true;
}

void ProceduralTexture::initPipeline()
{
    ComputePipelineConfiguration config;
    config.shader = m_shader.operator->();
    m_pipeline = std::make_shared<ComputePipeline>(config);
}

void ProceduralTexture::initCommandBuffer()
{
    auto &app = Application::getInstance();
    auto &vulkanContext = app.getVulkanContext();

    CommandPoolConfig poolConfig{};
    poolConfig.queueFamilyIndex = vulkanContext.getComputeQueueIndex();
    poolConfig.flags = 0;

    auto &rc = vulkanContext.getRenderContext();
    m_commandPoolHash = rc.commandPoolManager->createCommandPool(poolConfig);
}

void ProceduralTexture::initTexture()
{
    TextureSpecification spec;
    spec.width = TEXTURE_SIZE;
    spec.height = TEXTURE_SIZE;
    spec.depth = 1;
    spec.type = m_config.cubemap ? TextureType::TEXTURECUBE : TextureType::TEXTURE2D;
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

    auto asset = AssetManager::registerVirtualAsset(std::make_unique<ATexture>(std::move(texture)), textureName, ASSET_TEXTURE);
    m_textureAsset = asset.as<ATexture>();
    m_texture = m_textureAsset.operator->();
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

// Stores a member's annotated default value into the push-constant buffer at its offset
static void s_writeMemberDefault(std::vector<uint8_t> &buffer, const PushConstantMemberInfo &member)
{
    const auto &meta = member.metadata;
    if (!meta.hasDefault || meta.defaultValue.empty() || member.offset >= buffer.size()) {
        return;
    }

    uint8_t *dst = buffer.data() + member.offset;
    const auto &def = meta.defaultValue;
    auto get = [&](int i) { return static_cast<size_t>(i) < def.size() ? def[i] : 0.0f; };

    using BaseType = PushConstantMemberInfo::BaseType;
    switch (member.getBaseType()) {
    case BaseType::FLOAT: {
        float v = get(0);
        std::memcpy(dst, &v, 4);
        break;
    }
    case BaseType::INT: {
        int32_t v = static_cast<int32_t>(get(0));
        std::memcpy(dst, &v, 4);
        break;
    }
    case BaseType::UINT: {
        uint32_t v = static_cast<uint32_t>(get(0));
        std::memcpy(dst, &v, 4);
        break;
    }
    case BaseType::VEC2: {
        float v[2] = {get(0), get(1)};
        std::memcpy(dst, v, 8);
        break;
    }
    case BaseType::VEC3: {
        float v[3] = {get(0), get(1), get(2)};
        std::memcpy(dst, v, 12);
        break;
    }
    case BaseType::VEC4: {
        float v[4] = {get(0), get(1), get(2), get(3)};
        std::memcpy(dst, v, 16);
        break;
    }
    default:
        break;
    }
}

void ProceduralTexture::reflectParameters()
{
    m_parameters.clear();
    if (!m_shader) {
        return;
    }

    const auto &pushConstants = m_shader->getDetailedPushConstants();
    if (pushConstants.empty()) {
        return;
    }

    if (m_expectedPushConstantSize > 0) {
        m_pushConstantData.assign(m_expectedPushConstantSize, 0);
    }

    for (const auto &member : pushConstants[0].members) {
        ProceduralParameter param;
        param.name = member.name;
        param.displayName = member.metadata.displayName.empty() ? member.name : member.metadata.displayName;
        param.type = member.getBaseType();
        param.offset = member.offset;
        param.hasRange = member.metadata.hasRange;
        param.minValue = member.metadata.minValue;
        param.maxValue = member.metadata.maxValue;
        param.hidden = member.metadata.hidden;
        param.isColor = member.metadata.isColor;
        m_parameters.push_back(std::move(param));

        s_writeMemberDefault(m_pushConstantData, member);
    }
}

void ProceduralTexture::setParameterFloat(size_t index, double value)
{
    if (index >= m_parameters.size()) {
        return;
    }
    const ProceduralParameter &param = m_parameters[index];
    if (param.offset + sizeof(float) > m_pushConstantData.size()) {
        return;
    }
    float v = static_cast<float>(value);
    std::memcpy(m_pushConstantData.data() + param.offset, &v, sizeof(float));
}

double ProceduralTexture::getParameterFloat(size_t index) const
{
    if (index >= m_parameters.size()) {
        return 0.0;
    }
    const ProceduralParameter &param = m_parameters[index];
    if (param.offset + sizeof(float) > m_pushConstantData.size()) {
        return 0.0;
    }
    float v;
    std::memcpy(&v, m_pushConstantData.data() + param.offset, sizeof(float));
    return v;
}

void ProceduralTexture::setParameterInt(size_t index, int64_t value)
{
    if (index >= m_parameters.size()) {
        return;
    }
    const ProceduralParameter &param = m_parameters[index];
    if (param.offset + sizeof(int32_t) > m_pushConstantData.size()) {
        return;
    }
    if (param.type == PushConstantMemberInfo::BaseType::UINT) {
        uint32_t v = static_cast<uint32_t>(value);
        std::memcpy(m_pushConstantData.data() + param.offset, &v, sizeof(uint32_t));
    } else {
        int32_t v = static_cast<int32_t>(value);
        std::memcpy(m_pushConstantData.data() + param.offset, &v, sizeof(int32_t));
    }
}

int64_t ProceduralTexture::getParameterInt(size_t index) const
{
    if (index >= m_parameters.size()) {
        return 0;
    }
    const ProceduralParameter &param = m_parameters[index];
    if (param.offset + sizeof(int32_t) > m_pushConstantData.size()) {
        return 0;
    }
    if (param.type == PushConstantMemberInfo::BaseType::UINT) {
        uint32_t v;
        std::memcpy(&v, m_pushConstantData.data() + param.offset, sizeof(uint32_t));
        return static_cast<int64_t>(v);
    }
    int32_t v;
    std::memcpy(&v, m_pushConstantData.data() + param.offset, sizeof(int32_t));
    return v;
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

    auto &rc = Application::getInstance().getVulkanContext().getRenderContext();
    auto pool = rc.commandPoolManager->getCommandPool(m_commandPoolHash);
    auto commandBuffer = pool->getPrimaryCommandBuffer();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VkCommandBuffer vkCmd = commandBuffer->getCommandBufferVk();

    uint32_t layerCount = isCubeType(m_texture->getSpecification().type) ? 6 : 1;

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
    preBarrier.subresourceRange.layerCount = layerCount;
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
    vkCmdDispatch(vkCmd, workGroupsX, workGroupsY, layerCount);

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
    postBarrier.subresourceRange.layerCount = layerCount;
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

// Lazily imports a generator shader (caching its handle) and builds a ready generator from it
static std::unique_ptr<ProceduralTexture> s_makeGenerator(const char *shaderRelPath, AssetHandle &cachedHandle,
                                                          const ProceduralTextureConfig &config, const char *label)
{
    if (cachedHandle == 0) {
        auto shaderDir = EnginePaths::shaderDirectory();
        Ref<AShader> asset = AssetManager::importAsset<AShader>(shaderDir / shaderRelPath);
        if (!asset) {
            RP_CORE_ERROR("Failed to load {} shader", label);
            return nullptr;
        }
        cachedHandle = asset.get()->handle();
    }

    auto generator = std::make_unique<ProceduralTexture>(cachedHandle, config);
    if (!generator->isValid()) {
        RP_CORE_ERROR("Failed to create {} generator", label);
        return nullptr;
    }
    return generator;
}

std::unique_ptr<ProceduralTexture> ProceduralTexture::createWhiteNoiseGenerator(const ProceduralTextureConfig &config)
{
    static AssetHandle s_shaderHandle = 0;
    return s_makeGenerator("glsl/Generators/WhiteNoise.cs.glsl", s_shaderHandle, config, "WhiteNoise");
}

std::unique_ptr<ProceduralTexture> ProceduralTexture::createPerlinNoiseGenerator(const ProceduralTextureConfig &config)
{
    static AssetHandle s_shaderHandle = 0;
    return s_makeGenerator("glsl/Generators/PerlinNoise.cs.glsl", s_shaderHandle, config, "PerlinNoise");
}

std::unique_ptr<ProceduralTexture> ProceduralTexture::createSimplexNoiseGenerator(const ProceduralTextureConfig &config)
{
    static AssetHandle s_shaderHandle = 0;
    return s_makeGenerator("glsl/Generators/SimplexNoise.cs.glsl", s_shaderHandle, config, "SimplexNoise");
}

std::unique_ptr<ProceduralTexture> ProceduralTexture::createRidgedNoiseGenerator(const ProceduralTextureConfig &config)
{
    static AssetHandle s_shaderHandle = 0;
    return s_makeGenerator("glsl/Generators/RidgedNoise.cs.glsl", s_shaderHandle, config, "RidgedNoise");
}

Ref<ATexture> ProceduralTexture::generateWhiteNoise(uint32_t seed, const ProceduralTextureConfig &config)
{
    auto generator = createWhiteNoiseGenerator(config);
    if (!generator) {
        return {};
    }

    WhiteNoisePushConstants pc{};
    pc.seed = seed;
    generator->setPushConstants(pc);
    generator->generate();

    return generator->getTextureAsset();
}

Ref<ATexture> ProceduralTexture::generatePerlinNoise(const PerlinNoisePushConstants &params,
                                                         const ProceduralTextureConfig &config)
{
    auto generator = createPerlinNoiseGenerator(config);
    if (!generator) {
        return {};
    }

    generator->setPushConstants(params);
    generator->generate();

    return generator->getTextureAsset();
}

Ref<ATexture> ProceduralTexture::generateSimplexNoise(const SimplexNoisePushConstants &params,
                                                          const ProceduralTextureConfig &config)
{
    auto generator = createSimplexNoiseGenerator(config);
    if (!generator) {
        return {};
    }

    generator->setPushConstants(params);
    generator->generate();

    return generator->getTextureAsset();
}

Ref<ATexture> ProceduralTexture::generateRidgedNoise(const RidgedNoisePushConstants &params,
                                                         const ProceduralTextureConfig &config)
{
    auto generator = createRidgedNoiseGenerator(config);
    if (!generator) {
        return {};
    }

    generator->setPushConstants(params);
    generator->generate();

    return generator->getTextureAsset();
}

static AtmospherePushConstants s_buildAtmospherePushConstants(float timeOfDay, const AtmospherePushConstants *params)
{
    if (params) {
        return *params;
    }

    AtmospherePushConstants pc;
    glm::vec3 sunDir = Environment::sunDirection(timeOfDay, 0.0f, 0.0f);

    // Earth-like atmospheric defaults (real units; the shader holds the planet/atmosphere radii)
    pc.cameraPos = glm::vec3(0.0f);
    pc.innerRadius = 1.0f;
    pc.sunDirection = sunDir;
    pc.outerRadius = 1.025f;
    pc.cameraDir = glm::vec3(0.0f, 0.0f, -1.0f);
    pc.scaleDepth = 0.25f;
    pc.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    pc.kr = 0.0025f;
    pc.invWavelength = glm::vec3(5.8f, 13.5f, 33.1f); // Rayleigh betaR scaled by 1e-6 in the shader
    pc.km = 21.0f;                                    // Mie betaM scaled by 1e-6 in the shader
    pc.eSun = 20.0f;
    pc.g = 0.76f;
    pc.fovY = 1.5708f;
    pc.cameraAltitude = 1.0f; // meters above ground
    return pc;
}

Ref<ATexture> ProceduralTexture::generateAtmosphere(float timeOfDay, const AtmospherePushConstants *params,
                                                        const ProceduralTextureConfig &config)
{
    // Function-local static for shader handle - AssetManager handles caching
    static AssetHandle s_shaderHandle;

    if (s_shaderHandle == 0) {
        auto shaderDir = EnginePaths::shaderDirectory();

        Ref<AShader> asset = AssetManager::importAsset<AShader>(shaderDir / "glsl/Generators/Atmosphere.cs.glsl");
        if (!asset) {
            RP_CORE_ERROR("Failed to load Atmosphere shader");
            return {};
        }
        s_shaderHandle = asset.get()->handle();
    }

    // Use HDR format by default for atmospheric scattering
    ProceduralTextureConfig atmosphereConfig = config;
    if (atmosphereConfig.format == TextureFormat::RGBA8) {
        atmosphereConfig.format = TextureFormat::RGBA16F;
    }

    ProceduralTexture generator(s_shaderHandle, atmosphereConfig);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create atmosphere generator");
        return {};
    }

    AtmospherePushConstants pc = s_buildAtmospherePushConstants(timeOfDay, params);
    generator.setPushConstants(pc);
    generator.generate();

    return generator.getTextureAsset();
}

static AssetHandle s_getAtmosphereCubemapShaderHandle()
{
    static AssetHandle s_shaderHandle = 0;

    if (s_shaderHandle == 0) {
        auto shaderDir = EnginePaths::shaderDirectory();

        ShaderImportConfig importConfig;
        importConfig.compileInfo.macros.push_back(ShaderMacro("OUTPUT_CUBEMAP"));

        Ref<AShader> asset = AssetManager::importAsset<AShader>(shaderDir / "glsl/Generators/Atmosphere.cs.glsl", importConfig);
        if (!asset) {
            RP_CORE_ERROR("Failed to load Atmosphere cubemap shader");
            return 0;
        }
        s_shaderHandle = asset.get()->handle();
    }

    return s_shaderHandle;
}

Ref<ATexture> ProceduralTexture::generateAtmosphereCubemap(float timeOfDay, const AtmospherePushConstants *params,
                                                               const ProceduralTextureConfig &config)
{
    AssetHandle shaderHandle = s_getAtmosphereCubemapShaderHandle();
    if (shaderHandle == 0) {
        return {};
    }

    ProceduralTextureConfig cubemapConfig = config;
    cubemapConfig.cubemap = true;
    cubemapConfig.wrap = TextureWrap::ClampToEdge;
    if (cubemapConfig.format == TextureFormat::RGBA8) {
        cubemapConfig.format = TextureFormat::RGBA16F;
    }

    ProceduralTexture generator(shaderHandle, cubemapConfig);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create atmosphere cubemap generator");
        return {};
    }

    AtmospherePushConstants pc = s_buildAtmospherePushConstants(timeOfDay, params);
    generator.setPushConstants(pc);
    generator.generate();

    return generator.getTextureAsset();
}

void ProceduralTexture::regenerateAtmosphereCubemap(Texture &cube, float timeOfDay, const AtmospherePushConstants *params)
{
    AssetHandle shaderHandle = s_getAtmosphereCubemapShaderHandle();
    if (shaderHandle == 0) {
        return;
    }

    ProceduralTexture generator(shaderHandle, cube);
    if (!generator.isValid()) {
        RP_CORE_ERROR("Failed to create atmosphere cubemap regenerator");
        return;
    }

    AtmospherePushConstants pc = s_buildAtmospherePushConstants(timeOfDay, params);
    generator.setPushConstants(pc);
    generator.generate();
}

} // namespace Rapture
