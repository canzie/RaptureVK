#include "Renderpass.h"

#include "Logging/Log.h"

#include "WindowContext/Application.h"

#include <stdexcept>
#include <map>


namespace Rapture {

Renderpass::Renderpass()
    : m_renderPass(VK_NULL_HANDLE)
{

}

Renderpass::Renderpass(const std::vector<SubpassInfo>& subpassBuildInfo)
    : m_renderPass(VK_NULL_HANDLE), m_subpassBuildInfo(subpassBuildInfo)
{

    m_colorAttachmentReferences.resize(subpassBuildInfo.size());
    m_inputAttachmentReferences.resize(subpassBuildInfo.size());

    setUniqueAttachmentDescriptions(subpassBuildInfo);

    uint32_t subpassIndex = 0;
    // go over all subpasses and create a global attachement array
    for (const auto& subpass : subpassBuildInfo) {
        createSubpass(subpass, subpassIndex);
        subpassIndex++;
    }

    createSubpassDependencies(subpassBuildInfo);
    
    fillRenderPass();

}

Renderpass::~Renderpass()
{
    destroy();
}

void Renderpass::fillRenderPass()
{
    if (m_renderPass != VK_NULL_HANDLE) {
        destroy();
    }

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(m_attachmentDescriptions.size());
    renderPassInfo.pAttachments = m_attachmentDescriptions.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(m_subpassDescriptions.size());
    renderPassInfo.pSubpasses = m_subpassDescriptions.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(m_dependencies.size());
    renderPassInfo.pDependencies = m_dependencies.data();

    auto& app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create render pass!");
        throw std::runtime_error("failed to create render pass!");
    }
}

void Renderpass::destroy()
{
    if (m_renderPass != VK_NULL_HANDLE) {
        auto& app = Application::getInstance();
        VkDevice device = app.getVulkanContext().getLogicalDevice();
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    m_attachmentDescriptions.clear();
    m_subpassDescriptions.clear();
    m_dependencies.clear();
    m_colorAttachmentReferences.clear();
    m_inputAttachmentReferences.clear();


}


void Renderpass::createSubpass(const SubpassInfo &subpassInfo, uint32_t subpassIndex)
{
    VkSubpassDescription subpassDescription{};


    subpassDescription.pipelineBindPoint = subpassInfo.pipelineBindPoint;

    // gather all color attachments
    for (const auto& attachmentUsage : subpassInfo.colorAttachments) {
        m_colorAttachmentReferences[subpassIndex].push_back(attachmentUsage.attachmentReference);
    }

    // gather all input attachments
    for (const auto& attachmentUsage : subpassInfo.inputAttachments) {
        m_inputAttachmentReferences[subpassIndex].push_back(attachmentUsage.attachmentReference);
    }

    subpassDescription.colorAttachmentCount = m_colorAttachmentReferences[subpassIndex].size();
    subpassDescription.pColorAttachments = m_colorAttachmentReferences[subpassIndex].data();

    subpassDescription.inputAttachmentCount = m_inputAttachmentReferences[subpassIndex].size();
    subpassDescription.pInputAttachments = m_inputAttachmentReferences[subpassIndex].data();

    if (subpassInfo.depthStencilAttachment.has_value()) {
        subpassDescription.pDepthStencilAttachment = &subpassInfo.depthStencilAttachment->attachmentReference;
    }

    m_subpassDescriptions.push_back(subpassDescription);
}



void Renderpass::setUniqueAttachmentDescriptions(const std::vector<SubpassInfo>& subpassBuildInfo)
{
    std::map<uint32_t, VkAttachmentDescription> uniqueAttachments;
    uint32_t maxAttachmentIndex = 0;

    // Collect all unique attachments from all subpasses
    for (const auto& subpass : subpassBuildInfo) {
        // Process color attachments
        for (const auto& colorAttachment : subpass.colorAttachments) {
            uint32_t attachmentIndex = colorAttachment.attachmentReference.attachment;
            uniqueAttachments[attachmentIndex] = colorAttachment.attachmentDescription;
            maxAttachmentIndex = std::max(maxAttachmentIndex, attachmentIndex);
        }

        // Process depth attachment if present
        if (subpass.depthStencilAttachment) {
            uint32_t attachmentIndex = subpass.depthStencilAttachment->attachmentReference.attachment;
            uniqueAttachments[attachmentIndex] = subpass.depthStencilAttachment->attachmentDescription;
            maxAttachmentIndex = std::max(maxAttachmentIndex, attachmentIndex);
        }
    }

    // Check for missing indices and resize the attachment descriptions vector
    m_attachmentDescriptions.resize(maxAttachmentIndex + 1);

    // Fill the attachment descriptions in order and check for gaps
    for (uint32_t i = 0; i <= maxAttachmentIndex; ++i) {
        auto it = uniqueAttachments.find(i);
        if (it == uniqueAttachments.end()) {
            RP_CORE_WARN("Missing attachment at index {0} in renderpass configuration!, will lead to undefined behavior!", i);
            continue;
        }
        m_attachmentDescriptions[i] = it->second;
    }
}

void Renderpass::createSubpassDependencies(const std::vector<SubpassInfo>& subpassBuildInfo) 
{
    const size_t subpassCount = subpassBuildInfo.size();
    if (subpassCount == 0) return;

    // External -> First Subpass dependency
    {
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        
        // Wait for color attachment output and depth operations from external
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        
        // Before we write to color and depth attachments
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        m_dependencies.push_back(dependency);
    }

    // Dependencies between subpasses
    for (size_t i = 1; i < subpassCount; i++) {
        const auto& prevSubpass = subpassBuildInfo[i - 1];
        const auto& currentSubpass = subpassBuildInfo[i];

        VkSubpassDependency dependency{};
        dependency.srcSubpass = i - 1;
        dependency.dstSubpass = i;

        // Set up source stage and access masks based on previous subpass usage
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // Set up destination stage and access masks based on current subpass usage
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | 
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // If the current subpass has input attachments, add appropriate masks
        if (!currentSubpass.inputAttachments.empty()) {
            dependency.srcStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependency.dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependency.dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        }

        // If either subpass uses depth, add appropriate masks
        if (prevSubpass.depthStencilAttachment.has_value() || 
            currentSubpass.depthStencilAttachment.has_value()) {
            dependency.srcStageMask |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        m_dependencies.push_back(dependency);
    }

    // Check if we need a final external dependency by looking at attachment final layouts
    bool needsFinalDependency = false;
    
    // Check color attachments of last subpass
    const auto& lastSubpass = subpassBuildInfo[subpassCount - 1];
    for (const auto& attachment : lastSubpass.colorAttachments) {
        VkImageLayout finalLayout = attachment.attachmentDescription.finalLayout;
        if (finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
            finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            finalLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            needsFinalDependency = true;
            break;
        }
    }

    // Check depth attachment of last subpass
    if (lastSubpass.depthStencilAttachment) {
        VkImageLayout finalLayout = lastSubpass.depthStencilAttachment->attachmentDescription.finalLayout;
        if (finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            finalLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            needsFinalDependency = true;
        }
    }

    // Only add final external dependency if needed
    if (needsFinalDependency) {
        VkSubpassDependency dependency{};
        dependency.srcSubpass = subpassCount - 1;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        
        // After we write to attachments
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        // Before external operations
        dependency.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependency.dstAccessMask = 0;
        
        m_dependencies.push_back(dependency);
    }
}

}