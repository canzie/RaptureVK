#pragma once

#include <vulkan/vulkan.h>
#include <string>

namespace Rapture {


class PipelineBase {
public:


};

struct PipelineData {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    std::string name;
};






}

