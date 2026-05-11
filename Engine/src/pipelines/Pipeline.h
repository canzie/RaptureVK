#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace Rapture {


class PipelineBase {
public:
    virtual VkPipelineLayout getPipelineLayoutVk() const = 0;
    virtual VkPipeline getPipelineVk() const = 0;
    virtual VkPipelineBindPoint getPipelineBindPoint() const = 0;

protected:
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;
    VkPipelineBindPoint m_pipelineBindPoint;

};

struct PipelineData {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    std::string name;
};


struct FramebufferSpecification {
    std::vector<VkFormat> colorAttachments;
    VkFormat depthAttachment = VK_FORMAT_UNDEFINED;
    VkFormat stencilAttachment = VK_FORMAT_UNDEFINED;
    
    // Multiview support for rendering to multiple layers in one pass
    uint32_t viewMask = 0;  // Bitmask indicating which views to render
    uint32_t correlationMask = 0;  // Bitmask indicating correlated views for optimization
};



}

