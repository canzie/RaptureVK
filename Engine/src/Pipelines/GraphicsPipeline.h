#pragma once

#include "Pipeline.h"
#include "Shaders/Shader.h"

#include <vulkan/vulkan.h>
#include <optional>
#include <vector>
#include <memory>


namespace Rapture {



struct GraphicsPipelineConfiguration {
    

    VkPipelineVertexInputStateCreateInfo                  vertexInputState;
    VkPipelineDynamicStateCreateInfo                      dynamicState;
    VkPipelineViewportStateCreateInfo                     viewportState;
    VkPipelineInputAssemblyStateCreateInfo                inputAssemblyState;
    
    VkPipelineRasterizationStateCreateInfo                rasterizationState;
    
    VkPipelineMultisampleStateCreateInfo                  multisampleState;
   
    std::optional<VkPipelineDepthStencilStateCreateInfo>  depthStencilState;


    // References commonColorBlendAttachmentState
    VkPipelineColorBlendStateCreateInfo                   colorBlendState; 

    // e.g., VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, ...
    //std::vector<VkDynamicState>                         dynamicStates; 

    FramebufferSpecification                               framebufferSpec;
    std::shared_ptr<Shader>                                shader;


};



class GraphicsPipeline : public PipelineBase {
public:
    GraphicsPipeline(const GraphicsPipelineConfiguration& config);
    ~GraphicsPipeline();

    void buildPipelines(const GraphicsPipelineConfiguration& config);

    void bind(VkCommandBuffer commandBuffer);

    VkPipeline getPipelineVk() const { return m_pipeline; }
    VkPipelineLayout getPipelineLayoutVk() const { return m_pipelineLayout; }
    
private:
    void createPipelineLayout(const GraphicsPipelineConfiguration& config);
    void createPipeline(const GraphicsPipelineConfiguration& config);


protected:

    VkPipeline m_pipeline;
    VkPipelineLayout m_pipelineLayout;
};



}

