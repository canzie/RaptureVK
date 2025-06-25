#include "ComputePipeline.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include <stdexcept>
#include <glm/glm.hpp>


namespace Rapture {



ComputePipeline::ComputePipeline(const ComputePipelineConfiguration& config)
{
    buildPipelines(config);
}

ComputePipeline::~ComputePipeline()
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


void ComputePipeline::buildPipelines(const ComputePipelineConfiguration &config)
{
    createPipelineLayout(config);
    createPipeline(config);

}

void ComputePipeline::bind(VkCommandBuffer commandBuffer)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
}


void ComputePipeline::createPipelineLayout(const ComputePipelineConfiguration& config) {

    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.shader->getDescriptorSetLayouts().size());
    pipelineLayoutInfo.pSetLayouts = config.shader->getDescriptorSetLayouts().data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.shader->getPushConstantLayouts().size());
    pipelineLayoutInfo.pPushConstantRanges = config.shader->getPushConstantLayouts().data();

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        RP_CORE_ERROR("ComputePipeline::createPipelineLayout - failed to create pipeline layout!");
        throw std::runtime_error("ComputePipeline::createPipelineLayout - failed to create pipeline layout!");
    }

}

void ComputePipeline::createPipeline(const ComputePipelineConfiguration& config)
{
    auto& app = Application::getInstance();
    auto device = app.getVulkanContext().getLogicalDevice();

    // Get the compute shader stage from the shader
    const auto& stages = config.shader->getStages();
    VkPipelineShaderStageCreateInfo computeStage{};
    
    // Find the compute shader stage
    bool foundComputeStage = false;
    for (const auto& stage : stages) {
        if (stage.stage == VK_SHADER_STAGE_COMPUTE_BIT) {
            computeStage = stage;
            foundComputeStage = true;
            break;
        }
    }
    
    if (!foundComputeStage) {
        RP_CORE_ERROR("ComputePipeline::createPipeline - no compute shader stage found!");
        throw std::runtime_error("ComputePipeline::createPipeline - no compute shader stage found!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeStage;
    pipelineInfo.layout = m_pipelineLayout;
    
    // TODO: can be optimised to reuse pipelines
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        RP_CORE_ERROR("ComputePipeline::createPipeline - failed to create compute pipeline!");
        throw std::runtime_error("ComputePipeline::createPipeline - failed to create compute pipeline!");
    }
}


}