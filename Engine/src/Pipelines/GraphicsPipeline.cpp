#include "GraphicsPipeline.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include <stdexcept>


namespace Rapture {



GraphicsPipeline::GraphicsPipeline(const GraphicsPipelineConfiguration& config)
{
    buildPipelines(config);
}

GraphicsPipeline::~GraphicsPipeline()
{
    destroy();
}

void GraphicsPipeline::destroy() {
    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    for (auto& pipeline : m_pipelines) {
        vkDestroyPipeline(device, pipeline.pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipeline.pipelineLayout, nullptr);
    }
    m_pipelines.clear();
}

void GraphicsPipeline::buildPipelines(const GraphicsPipelineConfiguration &config)
{

    if (config.renderPass == nullptr) {
        RP_CORE_ERROR("GraphicsPipeline::buildPipelines - render pass is nullptr!");
        throw std::runtime_error("GraphicsPipeline::buildPipelines - render pass is nullptr!");
    }
    if (config.renderPass->getSubpassCount() == 0) {
        RP_CORE_ERROR("GraphicsPipeline::buildPipelines - render pass has no subpasses!");
        throw std::runtime_error("GraphicsPipeline::buildPipelines - render pass has no subpasses!");
    }


    destroy();
    m_pipelines.resize(config.renderPass->getSubpassCount());

    for (uint32_t i = 0; i < config.renderPass->getSubpassCount(); i++) {

        if (config.renderPass->getSubpassInfo(i).shaderProgram == nullptr) {
            RP_CORE_ERROR("GraphicsPipeline::buildPipelines - subpass {0} has no shader program!", i);
            throw std::runtime_error("GraphicsPipeline::buildPipelines - subpass has no shader program!");
        }

        createPipelineLayout(i);
        createPipeline(config, i);
        m_pipelines[i].name = config.renderPass->getSubpassInfo(i).name + " Graphics Pipeline";
    }

}

void GraphicsPipeline::bind(VkCommandBuffer commandBuffer, uint32_t subpassIndex)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[subpassIndex].pipeline);
}

void GraphicsPipeline::createPipelineLayout(uint32_t subpassIndex) {

    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    // TODO: add layouts based on the subpass shaders
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0; // Optional
    pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelines[subpassIndex].pipelineLayout) != VK_SUCCESS) {
        RP_CORE_ERROR("GraphicsPipeline::createPipelineLayout - failed to create pipeline layout!");
        throw std::runtime_error("GraphicsPipeline::createPipelineLayout - failed to create pipeline layout!");
    }

}

void GraphicsPipeline::createPipeline(const GraphicsPipelineConfiguration& config, uint32_t subpassIndex)
{

    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    auto subpassInfo = config.renderPass->getSubpassInfo(subpassIndex);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = subpassInfo.shaderProgram->getStages().size();
    pipelineInfo.pStages = subpassInfo.shaderProgram->getStages().data();

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

    pipelineInfo.layout = m_pipelines[subpassIndex].pipelineLayout;
    pipelineInfo.renderPass = config.renderPass->getRenderPassVk();
    pipelineInfo.subpass = subpassIndex;

    // TODO: can be optimised to reuse pipelines
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipelines[subpassIndex].pipeline) != VK_SUCCESS) {
        RP_CORE_ERROR("GraphicsPipeline::createPipeline - failed to create graphics pipeline!");
        throw std::runtime_error("GraphicsPipeline::createPipeline - failed to create graphics pipeline!");
    }
}


}