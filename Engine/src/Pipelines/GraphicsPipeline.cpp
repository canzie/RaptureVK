#include "GraphicsPipeline.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include <stdexcept>
#include <glm/glm.hpp>


namespace Rapture {



GraphicsPipeline::GraphicsPipeline(const GraphicsPipelineConfiguration& config)
{
    buildPipelines(config);
}

GraphicsPipeline::~GraphicsPipeline()
{
    auto& app = Application::getInstance();
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


void GraphicsPipeline::createPipelineLayout(const GraphicsPipelineConfiguration& config) {

    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    // TODO: add push constant reflection data to the shader
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // Accessible in vertex shader
    pushConstantRange.offset = 0;                              // Start at beginning
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec3);               // Size of model matrix (64 bytes)

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = config.shader->getDescriptorSetLayouts().size();
    pipelineLayoutInfo.pSetLayouts = config.shader->getDescriptorSetLayouts().data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;                    // We have 1 push constant range
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;     // Pointer to our range

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        RP_CORE_ERROR("GraphicsPipeline::createPipelineLayout - failed to create pipeline layout!");
        throw std::runtime_error("GraphicsPipeline::createPipelineLayout - failed to create pipeline layout!");
    }

}

void GraphicsPipeline::createPipeline(const GraphicsPipelineConfiguration& config)
{

    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    VkPipelineRenderingCreateInfoKHR pipelineDynamicRenderingInfo{};
    pipelineDynamicRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipelineDynamicRenderingInfo.colorAttachmentCount = config.framebufferSpec.colorAttachments.size();
    pipelineDynamicRenderingInfo.pColorAttachmentFormats = config.framebufferSpec.colorAttachments.data();
    pipelineDynamicRenderingInfo.depthAttachmentFormat = config.framebufferSpec.depthAttachment;
    pipelineDynamicRenderingInfo.stencilAttachmentFormat = config.framebufferSpec.stencilAttachment;


    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = config.shader->getStages().size();
    pipelineInfo.pStages = config.shader->getStages().data();

    pipelineInfo.pVertexInputState = &config.vertexInputState;
    pipelineInfo.pInputAssemblyState = &config.inputAssemblyState;
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
    pipelineInfo.basePipelineIndex = -1; // Optional

    pipelineInfo.pNext = &pipelineDynamicRenderingInfo;


    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        RP_CORE_ERROR("GraphicsPipeline::createPipeline - failed to create graphics pipeline!");
        throw std::runtime_error("GraphicsPipeline::createPipeline - failed to create graphics pipeline!");
    }
}


}