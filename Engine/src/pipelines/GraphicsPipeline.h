#pragma once

#include "Pipeline.h"
#include "shaders/Shader.h"

#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

struct GraphicsPipelineConfiguration {

    // Optional for mesh shaders (set to nullopt when using mesh/task shaders)
    std::optional<VkPipelineVertexInputStateCreateInfo> vertexInputState;
    std::optional<VkPipelineInputAssemblyStateCreateInfo> inputAssemblyState;

    VkPipelineDynamicStateCreateInfo dynamicState;
    VkPipelineViewportStateCreateInfo viewportState;

    VkPipelineRasterizationStateCreateInfo rasterizationState;

    VkPipelineMultisampleStateCreateInfo multisampleState;

    std::optional<VkPipelineDepthStencilStateCreateInfo> depthStencilState;

    // References commonColorBlendAttachmentState
    VkPipelineColorBlendStateCreateInfo colorBlendState;

    // e.g., VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, ...
    // std::vector<VkDynamicState>                         dynamicStates;

    FramebufferSpecification framebufferSpec;
    Shader *shader;
};

class GraphicsPipeline : public PipelineBase {
  public:
    GraphicsPipeline(const GraphicsPipelineConfiguration &config);
    ~GraphicsPipeline();

    void buildPipelines(const GraphicsPipelineConfiguration &config);

    void bind(VkCommandBuffer commandBuffer);

    VkPipeline getPipelineVk() const override { return m_pipeline; }
    VkPipelineLayout getPipelineLayoutVk() const override { return m_pipelineLayout; }
    VkPipelineBindPoint getPipelineBindPoint() const override { return VK_PIPELINE_BIND_POINT_GRAPHICS; }

  private:
    void createPipelineLayout(const GraphicsPipelineConfiguration &config);
    void createPipeline(const GraphicsPipelineConfiguration &config);
    void destroyPipeline();
    void rebuild();

    /**
     * @brief Deep-copies a config into owned storage so the pipeline can rebuild itself later
     *
     * The incoming config's create-infos point at caller temporaries; this re-homes the referenced
     * arrays into members and re-points the retained copy at them.
     * @param config The config to retain
     */
    void retainConfig(const GraphicsPipelineConfiguration &config);

  private:
    GraphicsPipelineConfiguration m_config;
    std::vector<VkDynamicState> m_dynamicStates;
    std::vector<VkViewport> m_viewports;
    std::vector<VkRect2D> m_scissors;
    std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachments;
    std::vector<VkVertexInputBindingDescription> m_vertexBindings;
    std::vector<VkVertexInputAttributeDescription> m_vertexAttributes;
    EventConnection m_shaderReloadConn;
};

} // namespace Rapture
