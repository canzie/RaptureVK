#include "renderer/atmosphere/SkyAtmosphere.h"

#include "gpu/descriptors/DescriptorManager.h"

#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetImportConfig.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <rapture/gpu/command_buffers/CommandPool.h>
#include <rapture/gpu/vulkan_context/VulkanQueue.h>
#include <rapture/scene/instances/Environment.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace Rapture {

static constexpr uint32_t TRANSMITTANCE_LUT_WIDTH = 256;
static constexpr uint32_t TRANSMITTANCE_LUT_HEIGHT = 64;
static constexpr uint32_t MULTIPLE_SCATTERING_LUT_SIZE = 32;
static constexpr uint32_t ENVIRONMENT_CUBEMAP_SIZE = 256;
static constexpr uint32_t WORKGROUP_SIZE = 8;

static constexpr float SUN_ANGULAR_RADIUS = 0.004651f;
static constexpr float OZONE_CENTER_KM = 25.0f;
static constexpr float OZONE_HALF_WIDTH_KM = 15.0f;
static constexpr glm::vec3 OZONE_ABSORPTION = glm::vec3(0.650f, 1.881f, 0.085f);

// The authored coefficients are in units of 1e-6 per metre, the shaders work per kilometre
static constexpr float PER_MEGAMETRE_TO_PER_KILOMETRE = 1e-3f;

static constexpr uint32_t TRANSMITTANCE_STEP_COUNT = 40;

static Ref<AShader> s_importAtmosphereShader(std::string_view fileName)
{
    auto shaderDir = EnginePaths::shaderDirectory();

    ShaderImportConfig config;
    config.compileInfo.includePath = shaderDir / "glsl";

    Ref<AShader> shader = AssetManager::importAsset<AShader>(shaderDir / "glsl/atmosphere" / fileName, config);
    if (!shader) {
        RP_CORE_ERROR("Failed to load atmosphere shader {}", fileName);
    }
    return shader;
}

static std::unique_ptr<DescriptorSet> s_createOutputAndUniformSet(Texture &output, UniformBuffer &uniforms)
{
    DescriptorSetBindings bindings;
    bindings.setNumber = 4;

    DescriptorSetBinding outputBinding{};
    outputBinding.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.location = DescriptorSetBindingLocation::CUSTOM_0;
    outputBinding.useStorageImageInfo = true;
    bindings.bindings.push_back(outputBinding);

    DescriptorSetBinding uniformBinding{};
    uniformBinding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBinding.location = DescriptorSetBindingLocation::CUSTOM_1;
    bindings.bindings.push_back(uniformBinding);

    auto set = std::make_unique<DescriptorSet>(bindings);
    set->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(output);
    set->getUniformBufferBinding(DescriptorSetBindingLocation::CUSTOM_1)->add(uniforms);
    return set;
}

static void s_recordBarriers(CommandBuffer *commandBuffer, std::initializer_list<VkImageMemoryBarrier2> barriers)
{
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.begin();
    vkCmdPipelineBarrier2(commandBuffer->getCommandBufferVk(), &dependency);
}

SkyAtmosphere::SkyAtmosphere()
{
    if (!loadShaders()) {
        return;
    }

    createTextures();

    auto &vc = Application::getInstance().getVulkanContext();
    m_uniformBuffer = std::make_unique<UniformBuffer>(sizeof(AtmosphereUniforms), BufferUsage::STREAM, vc.getVmaAllocator(), nullptr);

    createDescriptorSets();
    m_valid = true;
}

SkyAtmosphere::~SkyAtmosphere()
{
    m_environmentCubemapSet.reset();
    m_multipleScatteringSet.reset();
    m_transmittanceSet.reset();
    m_uniformBuffer.reset();
}

bool SkyAtmosphere::loadShaders()
{
    m_transmittanceShader = s_importAtmosphereShader("TransmittanceLut.cs.glsl");
    m_multipleScatteringShader = s_importAtmosphereShader("MultipleScatteringLut.cs.glsl");
    m_environmentCubemapShader = s_importAtmosphereShader("EnvironmentCubemap.cs.glsl");

    if (!m_transmittanceShader || !m_multipleScatteringShader || !m_environmentCubemapShader) {
        return false;
    }

    m_transmittancePipeline = std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_transmittanceShader.operator->()});
    m_multipleScatteringPipeline =
        std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_multipleScatteringShader.operator->()});
    m_environmentCubemapPipeline =
        std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_environmentCubemapShader.operator->()});
    return true;
}

void SkyAtmosphere::createTextures()
{
    TextureSpecification lutSpec;
    lutSpec.type = TextureType::TEXTURE2D;
    lutSpec.format = TextureFormat::RGBA16F;
    lutSpec.filter = TextureFilter::Linear;
    lutSpec.wrap = TextureWrap::ClampToEdge;
    lutSpec.srgb = false;
    lutSpec.storageImage = true;
    lutSpec.graphicsQueueOnly = true;

    lutSpec.width = TRANSMITTANCE_LUT_WIDTH;
    lutSpec.height = TRANSMITTANCE_LUT_HEIGHT;
    m_transmittanceLut = std::make_unique<Texture>(lutSpec);

    lutSpec.width = MULTIPLE_SCATTERING_LUT_SIZE;
    lutSpec.height = MULTIPLE_SCATTERING_LUT_SIZE;
    m_multipleScatteringLut = std::make_unique<Texture>(lutSpec);

    TextureSpecification cubeSpec;
    cubeSpec.type = TextureType::TEXTURECUBE;
    cubeSpec.format = TextureFormat::RGBA16F;
    cubeSpec.filter = TextureFilter::Linear;
    cubeSpec.wrap = TextureWrap::ClampToEdge;
    cubeSpec.srgb = false;
    cubeSpec.storageImage = true;
    cubeSpec.width = ENVIRONMENT_CUBEMAP_SIZE;
    cubeSpec.height = ENVIRONMENT_CUBEMAP_SIZE;

    static uint32_t s_cubemapCounter = 0;
    std::string name = "atmosphere_environment_" + std::to_string(s_cubemapCounter++);
    AssetRef asset = AssetManager::registerVirtualAsset(std::make_unique<ATexture>(std::make_unique<Texture>(cubeSpec)), name, ASSET_TEXTURE);
    m_environmentCubemap = asset.as<ATexture>();
}

void SkyAtmosphere::createDescriptorSets()
{
    m_transmittanceSet = s_createOutputAndUniformSet(*m_transmittanceLut, *m_uniformBuffer);
    m_multipleScatteringSet = s_createOutputAndUniformSet(*m_multipleScatteringLut, *m_uniformBuffer);
    m_environmentCubemapSet = s_createOutputAndUniformSet(m_environmentCubemap.get()->texture(), *m_uniformBuffer);
}

AtmosphereUniforms SkyAtmosphere::buildUniforms(const AtmosphereSettings &settings, const glm::vec3 &sunDirection,
                                                const glm::vec3 &sunIlluminance)
{
    AtmosphereUniforms uniforms{};
    uniforms.rayleighScattering =
        glm::vec4(settings.rayleighScattering * PER_MEGAMETRE_TO_PER_KILOMETRE, -1.0f / std::max(settings.rayleighScaleHeight, 1e-3f));
    uniforms.mieScattering =
        glm::vec4(glm::vec3(settings.mieScattering * PER_MEGAMETRE_TO_PER_KILOMETRE), -1.0f / std::max(settings.mieScaleHeight, 1e-3f));
    uniforms.mieExtinction =
        glm::vec4(glm::vec3((settings.mieScattering + settings.mieAbsorption) * PER_MEGAMETRE_TO_PER_KILOMETRE), settings.mieG);
    uniforms.ozoneAbsorption = glm::vec4(OZONE_ABSORPTION * settings.ozoneDensity * PER_MEGAMETRE_TO_PER_KILOMETRE, 0.0f);
    uniforms.groundAlbedo = glm::vec4(glm::vec3(settings.groundAlbedo), settings.groundRadius);
    uniforms.sunDirection = glm::vec4(sunDirection, settings.atmosphereThickness);
    uniforms.sunIlluminance = glm::vec4(sunIlluminance, std::cos(SUN_ANGULAR_RADIUS));
    uniforms.transmittanceLut = m_transmittanceLut->getBindlessIndex();
    uniforms.multipleScatteringLut = m_multipleScatteringLut->getBindlessIndex();
    uniforms.originAltitude = settings.originAltitude * 0.001f;
    return uniforms;
}

bool SkyAtmosphere::update(const AtmosphereSettings &settings, const glm::vec3 &sunDirection, const glm::vec3 &sunIlluminance)
{
    if (!m_valid) {
        return false;
    }

    AtmosphereUniforms uniforms = buildUniforms(settings, sunDirection, sunIlluminance);
    if (m_built && uniforms == m_uniforms) {
        return false;
    }

    RAPTURE_PROFILE_FUNCTION();

    m_uniforms = uniforms;
    m_uniformBuffer->addData(&m_uniforms, sizeof(AtmosphereUniforms), 0);

    auto &vc = Application::getInstance().getVulkanContext();
    auto &rc = vc.getRenderContext();
    CommandBufferLease commandBufferLease = rc.commandPoolManager->leasePrimary(vc.getGraphicsQueueIndex());
    CommandBuffer *commandBuffer = commandBufferLease.get();

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    recordBake(commandBuffer);
    commandBuffer->end();

    // The bake reuses one uniform buffer, so it completes before the next one may overwrite it
    auto queue = vc.getGraphicsQueue();
    queue->submitQueue(commandBuffer);
    queue->waitIdle();

    m_built = true;
    return true;
}

void SkyAtmosphere::recordBake(CommandBuffer *commandBuffer)
{
    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();
    auto &rc = Application::getInstance().getVulkanContext().getRenderContext();
    Texture &cubemap = m_environmentCubemap.get()->texture();

    s_recordBarriers(commandBuffer, {m_transmittanceLut->getBarrier2(TEXTURE_USAGE_STORAGE_COMPUTE, true)});

    m_transmittancePipeline->bind(cmd);
    rc.descriptorManager->bindSet(3, commandBuffer, m_transmittancePipeline);
    m_transmittanceSet->bind(cmd, m_transmittancePipeline);
    vkCmdDispatch(cmd, (TRANSMITTANCE_LUT_WIDTH + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE,
                  (TRANSMITTANCE_LUT_HEIGHT + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1);

    s_recordBarriers(commandBuffer, {m_transmittanceLut->getBarrier2(TEXTURE_USAGE_SAMPLED_COMPUTE),
                                     m_multipleScatteringLut->getBarrier2(TEXTURE_USAGE_STORAGE_COMPUTE, true)});

    m_multipleScatteringPipeline->bind(cmd);
    rc.descriptorManager->bindSet(3, commandBuffer, m_multipleScatteringPipeline);
    m_multipleScatteringSet->bind(cmd, m_multipleScatteringPipeline);
    vkCmdDispatch(cmd, MULTIPLE_SCATTERING_LUT_SIZE, MULTIPLE_SCATTERING_LUT_SIZE, 1);

    s_recordBarriers(commandBuffer, {m_multipleScatteringLut->getBarrier2(TEXTURE_USAGE_SAMPLED_COMPUTE),
                                     cubemap.getBarrier2(TEXTURE_USAGE_STORAGE_COMPUTE, true)});

    m_environmentCubemapPipeline->bind(cmd);
    rc.descriptorManager->bindSet(3, commandBuffer, m_environmentCubemapPipeline);
    m_environmentCubemapSet->bind(cmd, m_environmentCubemapPipeline);
    uint32_t cubeGroups = (ENVIRONMENT_CUBEMAP_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, cubeGroups, cubeGroups, 6);

    s_recordBarriers(commandBuffer, {cubemap.getBarrier2(TEXTURE_USAGE_SAMPLED_FRAGMENT)});
}

glm::vec3 SkyAtmosphere::transmittanceToSun(const AtmosphereSettings &settings, const glm::vec3 &sunDirection)
{
    // Positions are relative to the ground point below the origin, as in the shaders, in double precision since
    // the squared radii cancel
    const double groundRadius = settings.groundRadius;
    const double top = settings.atmosphereThickness;
    const double altitude = std::max(static_cast<double>(settings.originAltitude) * 0.001, 0.0);
    const glm::dvec3 dir = glm::normalize(glm::dvec3(sunDirection));
    const glm::dvec3 center = glm::dvec3(0.0, -groundRadius - altitude, 0.0);

    auto intersectSphere = [&](double radius, double &far) {
        double b = glm::dot(-center, dir);
        double c = glm::dot(center, center) - radius * radius;
        double discriminant = b * b - c;
        if (discriminant < 0.0) {
            return false;
        }
        double root = std::sqrt(discriminant);
        if (-b - root > 0.0) {
            far = -b - root;
            return true;
        }
        far = -b + root;
        return far > 0.0;
    };

    double groundHit = 0.0;
    if (intersectSphere(groundRadius, groundHit)) {
        return glm::vec3(0.0f);
    }

    double topHit = 0.0;
    if (!intersectSphere(groundRadius + top, topHit)) {
        return glm::vec3(1.0f);
    }

    const glm::dvec3 rayleigh = glm::dvec3(settings.rayleighScattering) * static_cast<double>(PER_MEGAMETRE_TO_PER_KILOMETRE);
    const double mieExtinction = (settings.mieScattering + settings.mieAbsorption) * static_cast<double>(PER_MEGAMETRE_TO_PER_KILOMETRE);
    const glm::dvec3 ozone = glm::dvec3(OZONE_ABSORPTION) * static_cast<double>(settings.ozoneDensity * PER_MEGAMETRE_TO_PER_KILOMETRE);

    glm::dvec3 opticalDepth(0.0);
    double dt = topHit / TRANSMITTANCE_STEP_COUNT;
    for (uint32_t step = 0; step < TRANSMITTANCE_STEP_COUNT; ++step) {
        glm::dvec3 samplePos = dir * ((static_cast<double>(step) + 0.5) * dt);
        double sampleAltitude = std::max(glm::length(samplePos - center) - groundRadius, 0.0);

        double rayleighDensity = std::exp(-sampleAltitude / std::max(static_cast<double>(settings.rayleighScaleHeight), 1e-3));
        double mieDensity = std::exp(-sampleAltitude / std::max(static_cast<double>(settings.mieScaleHeight), 1e-3));
        double ozoneDensity = std::max(0.0, 1.0 - std::abs(sampleAltitude - OZONE_CENTER_KM) / OZONE_HALF_WIDTH_KM);

        opticalDepth += (rayleigh * rayleighDensity + glm::dvec3(mieExtinction * mieDensity) + ozone * ozoneDensity) * dt;
    }

    return glm::vec3(glm::exp(-opticalDepth));
}

} // namespace Rapture
