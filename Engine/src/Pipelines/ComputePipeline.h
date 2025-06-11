#pragma once

#include "Pipeline.h"
#include "Shaders/Shader.h"

#include <vulkan/vulkan.h>
#include <optional>
#include <vector>
#include <memory>


namespace Rapture {

struct ComputePipelineConfiguration {
    std::shared_ptr<Shader> shader;
};


class ComputePipeline : public PipelineBase {
public:
    ComputePipeline(const ComputePipelineConfiguration& config);
    ~ComputePipeline();

    void buildPipelines(const ComputePipelineConfiguration& config);

    void bind(VkCommandBuffer commandBuffer);

    VkPipeline getPipelineVk() const { return m_pipeline; }
    VkPipelineLayout getPipelineLayoutVk() const { return m_pipelineLayout; }
    
private:
    void createPipelineLayout(const ComputePipelineConfiguration& config);
    void createPipeline(const ComputePipelineConfiguration& config);


protected:

    VkPipeline m_pipeline;
    VkPipelineLayout m_pipelineLayout;
};

}