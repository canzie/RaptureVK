#pragma once

#include "Pipeline.h"
#include "Shaders/Shader.h"

#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

struct ComputePipelineConfiguration {
    std::shared_ptr<Shader> shader;
};

class ComputePipeline : public PipelineBase {
  public:
    ComputePipeline(const ComputePipelineConfiguration &config);
    ~ComputePipeline();

    void buildPipelines(const ComputePipelineConfiguration &config);

    void bind(VkCommandBuffer commandBuffer);

    VkPipeline getPipelineVk() const override { return m_pipeline; }
    VkPipelineLayout getPipelineLayoutVk() const override { return m_pipelineLayout; }
    VkPipelineBindPoint getPipelineBindPoint() const override { return VK_PIPELINE_BIND_POINT_COMPUTE; }

  private:
    void createPipelineLayout(const ComputePipelineConfiguration &config);
    void createPipeline(const ComputePipelineConfiguration &config);
};

} // namespace Rapture