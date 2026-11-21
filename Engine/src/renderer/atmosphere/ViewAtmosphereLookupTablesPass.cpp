#include "renderer/atmosphere/ViewAtmosphereLookupTablesPass.h"

#include "gpu/descriptors/DescriptorManager.h"
#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetImportConfig.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/TracyProfiler.h>
#include <rapture/core/utils/rp_assert.h>
#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/render_data/SceneRenderData.h>
#include <rapture/scene/systems/Transforms.h>

#include <algorithm>
#include <cmath>

namespace Rapture {

static constexpr uint32_t SKY_VIEW_LUT_WIDTH = 192;
static constexpr uint32_t SKY_VIEW_LUT_HEIGHT = 108;
static constexpr uint32_t AERIAL_PERSPECTIVE_SIZE = 32;
static constexpr uint32_t WORKGROUP_SIZE = 8;

// The lowest altitude a camera is placed at, matching atmosphereLocalFromWorld
static constexpr float MIN_VIEWER_ALTITUDE_KM = 0.001f;

// How far the camera climbs or sinks before the sky around it is rebuilt
static constexpr float SKY_VIEW_ALTITUDE_TOLERANCE_KM = 0.001f;

struct SkyViewPushConstants {
    alignas(4) float viewerAltitude;
};

struct AerialPerspectivePushConstants {
    alignas(4) glm::vec4 cameraPosition;
    alignas(4) uint32_t cameraSSBOIndex;
    alignas(4) uint32_t cameraSlotIndex;
};

static Ref<AShader> s_importAtmosphereShader(std::string_view fileName)
{
    auto shaderDir = EnginePaths::shaderDirectory();

    ShaderImportConfig config;
    config.compileInfo.includePath = shaderDir / "glsl";

    Ref<AShader> shader = AssetManager::importAsset<AShader>(shaderDir / "glsl/atmosphere" / fileName, config);
    RP_ASSERT(shader, "Atmosphere shader failed to load");
    if (!shader) {
        RP_CORE_ERROR("Failed to load atmosphere shader {}", fileName);
    }
    return shader;
}

static DescriptorSetBinding s_uniformBinding()
{
    DescriptorSetBinding binding{};
    binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.location = DescriptorSetBindingLocation::CUSTOM_1;
    return binding;
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
    bindings.bindings.push_back(s_uniformBinding());

    auto set = std::make_unique<DescriptorSet>(bindings);
    set->getTextureBinding(DescriptorSetBindingLocation::CUSTOM_0)->add(output);
    set->getUniformBufferBinding(DescriptorSetBindingLocation::CUSTOM_1)->add(uniforms);
    return set;
}

ViewAtmosphereLookupTablesPass::ViewAtmosphereLookupTablesPass(uint32_t framesInFlight) : m_framesInFlight(framesInFlight)
{
    m_rc = &Application::getInstance().getVulkanContext().getRenderContext();

    loadShaders();
    createTextures();
    createDescriptorSets();
}

ViewAtmosphereLookupTablesPass::~ViewAtmosphereLookupTablesPass()
{
    m_uniformSets.clear();
    m_aerialPerspectiveSets.clear();
    m_skyViewSets.clear();
    m_uniformBuffers.clear();
}

void ViewAtmosphereLookupTablesPass::loadShaders()
{
    m_skyViewShader = s_importAtmosphereShader("SkyViewLut.cs.glsl");
    m_aerialPerspectiveShader = s_importAtmosphereShader("AerialPerspectiveVolume.cs.glsl");

    if (m_skyViewShader) {
        m_skyViewPipeline = std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_skyViewShader.operator->()});
    }
    if (m_aerialPerspectiveShader) {
        m_aerialPerspectivePipeline =
            std::make_shared<ComputePipeline>(ComputePipelineConfiguration{m_aerialPerspectiveShader.operator->()});
    }
}

void ViewAtmosphereLookupTablesPass::createTextures()
{
    TextureSpecification spec;
    spec.format = TextureFormat::RGBA16F;
    spec.filter = TextureFilter::Linear;
    spec.wrap = TextureWrap::ClampToEdge;
    spec.srgb = false;
    spec.storageImage = true;
    spec.graphicsQueueOnly = true;

    spec.type = TextureType::TEXTURE2D;
    spec.width = SKY_VIEW_LUT_WIDTH;
    spec.height = SKY_VIEW_LUT_HEIGHT;
    m_skyViewLut = std::make_unique<Texture>(spec);
    m_skyViewLut->getBindlessIndex();

    spec.type = TextureType::TEXTURE3D;
    spec.width = AERIAL_PERSPECTIVE_SIZE;
    spec.height = AERIAL_PERSPECTIVE_SIZE;
    spec.depth = AERIAL_PERSPECTIVE_SIZE;
    m_aerialPerspectiveVolume = std::make_unique<Texture>(spec);
    m_aerialPerspectiveVolume->getBindlessIndex();
}

void ViewAtmosphereLookupTablesPass::createDescriptorSets()
{
    VmaAllocator allocator = Application::getInstance().getVulkanContext().getVmaAllocator();

    for (uint32_t frame = 0; frame < m_framesInFlight; ++frame) {
        auto uniformBuffer = std::make_unique<UniformBuffer>(sizeof(AtmosphereUniforms), BufferUsage::STREAM, allocator, nullptr);

        m_skyViewSets.push_back(s_createOutputAndUniformSet(*m_skyViewLut, *uniformBuffer));
        m_aerialPerspectiveSets.push_back(s_createOutputAndUniformSet(*m_aerialPerspectiveVolume, *uniformBuffer));

        DescriptorSetBindings bindings;
        bindings.setNumber = 4;
        bindings.bindings.push_back(s_uniformBinding());
        auto uniformSet = std::make_unique<DescriptorSet>(bindings);
        uniformSet->getUniformBufferBinding(DescriptorSetBindingLocation::CUSTOM_1)->add(*uniformBuffer);
        m_uniformSets.push_back(std::move(uniformSet));

        m_uniformBuffers.push_back(std::move(uniformBuffer));
    }
}

void ViewAtmosphereLookupTablesPass::onResize(uint32_t width, uint32_t height)
{
    // the tables have a fixed resolution, independent of the view's
    (void)width;
    (void)height;
}

bool ViewAtmosphereLookupTablesPass::prepare(const RenderPassContext &context, const SkyAtmosphere *atmosphere)
{
    if (atmosphere != m_atmosphere) {
        m_atmosphere = atmosphere;
        m_skyViewStale = true;
        invalidateResources();
    }

    if (m_atmosphere == nullptr || !m_skyViewPipeline || !m_aerialPerspectivePipeline || !context.camera.isValid()) {
        return false;
    }

    const TransformComponent *cameraTransform = context.camera.tryRead<TransformComponent>();
    uint32_t cameraSlot = context.scene->getRenderData()->getCameraSlot(context.camera.getEntity());
    if (cameraTransform == nullptr || cameraSlot == UINT32_MAX) {
        return false;
    }

    m_cameraPosition = transform::translation(cameraTransform->world);
    m_cameraSlot = cameraSlot;

    AtmosphereUniforms uniforms = m_atmosphere->getUniforms();
    m_uniformBuffers[context.frameInFlight]->addData(&uniforms, sizeof(AtmosphereUniforms), 0);

    float altitude = std::max(m_cameraPosition.y * 0.001f + uniforms.originAltitude, MIN_VIEWER_ALTITUDE_KM);
    if (!(uniforms == m_skyViewUniforms) || std::abs(altitude - m_skyViewAltitude) > SKY_VIEW_ALTITUDE_TOLERANCE_KM) {
        m_skyViewUniforms = uniforms;
        m_skyViewAltitude = altitude;
        m_skyViewStale = true;
    }

    return true;
}

void ViewAtmosphereLookupTablesPass::updateResources(const RenderPassContext &context)
{
    (void)context;
    m_resources.clear();

    if (m_atmosphere == nullptr) {
        return;
    }

    m_resources.push_back({m_atmosphere->getTransmittanceLut(), ComputeResourceAccess::READ});
    m_resources.push_back({m_atmosphere->getMultipleScatteringLut(), ComputeResourceAccess::READ});
    m_resources.push_back({m_skyViewLut.get(), ComputeResourceAccess::WRITE});

    ComputeResource aerialPerspective;
    aerialPerspective.texture = m_aerialPerspectiveVolume.get();
    aerialPerspective.access = ComputeResourceAccess::WRITE;
    aerialPerspective.discardContents = true;
    m_resources.push_back(aerialPerspective);
}

void ViewAtmosphereLookupTablesPass::record(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    if (m_atmosphere == nullptr) {
        return;
    }

    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();
    const uint32_t frame = context.frameInFlight;

    if (m_skyViewStale) {
        m_skyViewPipeline->bind(cmd);
        m_rc->descriptorManager->bindSet(3, commandBuffer, m_skyViewPipeline);
        m_skyViewSets[frame]->bind(cmd, m_skyViewPipeline);

        SkyViewPushConstants pushConstants{};
        pushConstants.viewerAltitude = m_skyViewAltitude;
        vkCmdPushConstants(cmd, m_skyViewPipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkyViewPushConstants),
                           &pushConstants);

        vkCmdDispatch(cmd, groupCount(SKY_VIEW_LUT_WIDTH, WORKGROUP_SIZE), groupCount(SKY_VIEW_LUT_HEIGHT, WORKGROUP_SIZE), 1);
        m_skyViewStale = false;
    }

    m_aerialPerspectivePipeline->bind(cmd);
    m_rc->descriptorManager->bindSet(0, commandBuffer, m_aerialPerspectivePipeline);
    m_rc->descriptorManager->bindSet(3, commandBuffer, m_aerialPerspectivePipeline);
    m_aerialPerspectiveSets[frame]->bind(cmd, m_aerialPerspectivePipeline);

    AerialPerspectivePushConstants pushConstants{};
    pushConstants.cameraPosition = glm::vec4(m_cameraPosition, 1.0f);
    pushConstants.cameraSSBOIndex = context.scene->getRenderData()->getCameras().getDescriptorIndex(frame);
    pushConstants.cameraSlotIndex = m_cameraSlot;
    vkCmdPushConstants(cmd, m_aerialPerspectivePipeline->getPipelineLayoutVk(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(AerialPerspectivePushConstants), &pushConstants);

    uint32_t groups = groupCount(AERIAL_PERSPECTIVE_SIZE, WORKGROUP_SIZE);
    vkCmdDispatch(cmd, groups, groups, AERIAL_PERSPECTIVE_SIZE);
}

} // namespace Rapture
