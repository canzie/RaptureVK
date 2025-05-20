#pragma once

#include "Shaders/Shader.h"

#include "vulkan/vulkan.h"

#include <optional>
#include <vector>

namespace Rapture {


struct SubpassAttachmentUsage {
    VkAttachmentDescription attachmentDescription;
    VkAttachmentReference attachmentReference;
};

struct SubpassInfo {
    std::vector<SubpassAttachmentUsage> colorAttachments;
    std::vector<SubpassAttachmentUsage> inputAttachments;
    
    std::optional<SubpassAttachmentUsage> depthStencilAttachment;

    VkPipelineBindPoint pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // info about the shaders
    std::shared_ptr<Shader> shaderProgram;

    std::string name = "unnamed subpass";
    

};

    class Renderpass {
        public:
            Renderpass();
            Renderpass(const std::vector<SubpassInfo>& subpassBuildInfo);
            ~Renderpass();

            void fillRenderPass();
            void destroy();

            void createSubpass(const SubpassInfo& subpassInfo, uint32_t subpassIndex);
            uint32_t getSubpassCount() const { return m_subpassBuildInfo.size(); }
            const SubpassInfo& getSubpassInfo(uint32_t index) const { return m_subpassBuildInfo[index]; }

            VkRenderPass getRenderPassVk() const { return m_renderPass; }

    private:
        void setUniqueAttachmentDescriptions(const std::vector<SubpassInfo>& subpassBuildInfo);
        void createSubpassDependencies(const std::vector<SubpassInfo>& subpassBuildInfo);

        private:


            VkRenderPass m_renderPass;
            std::vector<VkAttachmentDescription> m_attachmentDescriptions;
            std::vector<VkSubpassDescription> m_subpassDescriptions;
            std::vector<VkSubpassDependency> m_dependencies;
            // each subpass has its own vector of attachment references, the depth attachements get references
            // from the original subpassBuildInfo
            std::vector<std::vector<VkAttachmentReference>> m_colorAttachmentReferences;
            std::vector<std::vector<VkAttachmentReference>> m_inputAttachmentReferences;

            std::vector<SubpassInfo> m_subpassBuildInfo;

    };
}
