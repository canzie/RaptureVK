#pragma once

#include "Pipeline.h"

#include "RenderTargets/FrameBuffers/Renderpass.h"

#include <vulkan/vulkan.h>
#include <optional>
#include <vector>
#include <memory>
namespace Rapture {



struct GraphicsPipelineConfiguration {
    

    VkPipelineVertexInputStateCreateInfo                vertexInputState;
    VkPipelineDynamicStateCreateInfo                    dynamicState;
    VkPipelineViewportStateCreateInfo                   viewportState;
    VkPipelineInputAssemblyStateCreateInfo              inputAssemblyState;
    
    VkPipelineRasterizationStateCreateInfo              rasterizationState;
    
    VkPipelineMultisampleStateCreateInfo                multisampleState;
   
    std::optional<VkPipelineDepthStencilStateCreateInfo>  depthStencilState;


    VkPipelineColorBlendAttachmentState                 commonColorBlendAttachmentState;
    // References commonColorBlendAttachmentState
    VkPipelineColorBlendStateCreateInfo                 colorBlendState; 

    // e.g., VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    //std::vector<VkDynamicState>                         dynamicStates; 

    std::shared_ptr<Renderpass>                         renderPass;
};



class GraphicsPipeline : public PipelineBase {
public:
    GraphicsPipeline(const GraphicsPipelineConfiguration& config);
    ~GraphicsPipeline();

    void destroy();

    void buildPipelines(const GraphicsPipelineConfiguration& config);

    void bind(VkCommandBuffer commandBuffer, uint32_t subpassIndex);
    
private:
    void createPipelineLayout(uint32_t subpassIndex);
    void createPipeline(const GraphicsPipelineConfiguration& config, uint32_t subpassIndex);


private:

    std::vector<PipelineData> m_pipelines;
};



}

