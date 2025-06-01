#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace Rapture {


class PipelineBase {
public:


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

};



}

