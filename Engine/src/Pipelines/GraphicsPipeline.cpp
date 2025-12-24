#include "GraphicsPipeline.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include <glm/glm.hpp>
#include <stdexcept>

namespace Rapture {

GraphicsPipeline::GraphicsPipeline(const GraphicsPipelineConfiguration &config)
{
    buildPipelines(config);
}

GraphicsPipeline::~GraphicsPipeline()
{
    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }

    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
}

void GraphicsPipeline::buildPipelines(const GraphicsPipelineConfiguration &config)
{
    createPipelineLayout(config);
    createPipeline(config);
}

void GraphicsPipeline::bind(VkCommandBuffer commandBuffer)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

void GraphicsPipeline::createPipelineLayout(const GraphicsPipelineConfiguration &config)
{

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.shader->getDescriptorSetLayouts().size());
    pipelineLayoutInfo.pSetLayouts = config.shader->getDescriptorSetLayouts().data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.shader->getPushConstantLayouts().size());
    pipelineLayoutInfo.pPushConstantRanges = config.shader->getPushConstantLayouts().data();

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create pipeline layout!");
        throw std::runtime_error("failed to create pipeline layout!");
    }
}

void GraphicsPipeline::createPipeline(const GraphicsPipelineConfiguration &config)
{

    auto &app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    VkPipelineRenderingCreateInfoKHR pipelineDynamicRenderingInfo{};
    pipelineDynamicRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipelineDynamicRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(config.framebufferSpec.colorAttachments.size());
    pipelineDynamicRenderingInfo.pColorAttachmentFormats = config.framebufferSpec.colorAttachments.data();
    pipelineDynamicRenderingInfo.depthAttachmentFormat = config.framebufferSpec.depthAttachment;
    pipelineDynamicRenderingInfo.stencilAttachmentFormat = config.framebufferSpec.stencilAttachment;

    // Add multiview support
    pipelineDynamicRenderingInfo.viewMask = config.framebufferSpec.viewMask;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(config.shader->getStages().size());
    pipelineInfo.pStages = config.shader->getStages().data();

    // Vertex input and input assembly are optional (not used for mesh shaders)
    pipelineInfo.pVertexInputState = config.vertexInputState.has_value() ? &config.vertexInputState.value() : nullptr;
    pipelineInfo.pInputAssemblyState = config.inputAssemblyState.has_value() ? &config.inputAssemblyState.value() : nullptr;
    pipelineInfo.pViewportState = &config.viewportState;
    pipelineInfo.pRasterizationState = &config.rasterizationState;
    pipelineInfo.pMultisampleState = &config.multisampleState;
    if (config.depthStencilState.has_value()) {
        pipelineInfo.pDepthStencilState = &config.depthStencilState.value();
    }
    pipelineInfo.pColorBlendState = &config.colorBlendState;
    pipelineInfo.pDynamicState = &config.dynamicState;

    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    // TODO: can be optimised to reuse pipelines
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1;              // Optional

    pipelineInfo.pNext = &pipelineDynamicRenderingInfo;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create graphics pipeline!");
        throw std::runtime_error("failed to create graphics pipeline!");
    }
}

} // namespace Rapture