#include "renderer/passes/RenderPass.h"

#include "textures/Texture.h"
#include "utils/rp_assert.h"

namespace Rapture {

static VkAttachmentLoadOp s_toVkLoadOp(RenderPassAttachmentLoadOp loadOp)
{
    switch (loadOp) {
    case RenderPassAttachmentLoadOp::LOAD:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case RenderPassAttachmentLoadOp::CLEAR:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case RenderPassAttachmentLoadOp::DONT_CARE:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    return VK_ATTACHMENT_LOAD_OP_LOAD;
}

static VkAttachmentStoreOp s_toVkStoreOp(RenderPassAttachmentStoreOp storeOp)
{
    switch (storeOp) {
    case RenderPassAttachmentStoreOp::STORE:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case RenderPassAttachmentStoreOp::DONT_CARE:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    return VK_ATTACHMENT_STORE_OP_STORE;
}

static VkRenderingAttachmentInfo s_buildColorAttachmentInfo(const RenderPassAttachment &attachment)
{
    VkRenderingAttachmentInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = attachment.texture->getImageView();
    info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    info.loadOp = s_toVkLoadOp(attachment.loadOp);
    info.storeOp = s_toVkStoreOp(attachment.storeOp);

    if (attachment.loadOp == RenderPassAttachmentLoadOp::CLEAR) {
        info.clearValue.color.float32[0] = attachment.clearColor.r;
        info.clearValue.color.float32[1] = attachment.clearColor.g;
        info.clearValue.color.float32[2] = attachment.clearColor.b;
        info.clearValue.color.float32[3] = attachment.clearColor.a;
    }

    return info;
}

static VkRenderingAttachmentInfo s_buildDepthAttachmentInfo(const RenderPassAttachment &attachment)
{
    VkRenderingAttachmentInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = attachment.texture->getImageView();
    info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    info.loadOp = s_toVkLoadOp(attachment.loadOp);
    info.storeOp = s_toVkStoreOp(attachment.storeOp);

    if (attachment.loadOp == RenderPassAttachmentLoadOp::CLEAR) {
        info.clearValue.depthStencil.depth = attachment.clearDepth;
    }

    return info;
}

static VkRenderingAttachmentInfo s_buildStencilAttachmentInfo(const RenderPassAttachment &attachment)
{
    VkRenderingAttachmentInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = attachment.texture->getImageView();
    info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    info.loadOp = s_toVkLoadOp(attachment.loadOp);
    info.storeOp = s_toVkStoreOp(attachment.storeOp);

    if (attachment.loadOp == RenderPassAttachmentLoadOp::CLEAR) {
        info.clearValue.depthStencil.stencil = attachment.clearStencil;
    }

    return info;
}

// The writes a layout implies, so a barrier out of it makes them available
static VkAccessFlags s_writeAccessForLayout(VkImageLayout layout)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_GENERAL:
        return VK_ACCESS_SHADER_WRITE_BIT;
    default:
        return 0;
    }
}

static void s_appendTransitionBarrier(std::vector<VkImageMemoryBarrier> &barriers, const RenderPassAttachment &attachment,
                                      VkImageLayout newLayout, VkAccessFlags dstAccessMask)
{
    if (attachment.texture == nullptr) {
        return;
    }

    // A cleared or discarded attachment has nothing worth preserving, so it transitions from
    // UNDEFINED. A LOAD attachment keeps its contents and so comes from the layout it tracks.
    const VkImageLayout oldLayout =
        attachment.loadOp == RenderPassAttachmentLoadOp::LOAD ? attachment.texture->getCurrentLayout() : VK_IMAGE_LAYOUT_UNDEFINED;

    // Emitted even when the layout does not change, since it still carries the dependency on
    // whichever pass last wrote the attachment
    barriers.push_back(
        attachment.texture->getImageMemoryBarrier(oldLayout, newLayout, s_writeAccessForLayout(oldLayout), dstAccessMask));
}

const RenderPassAttachments &RenderPass::getAttachments(const RenderPassContext &context)
{
    if (m_attachmentsFrame != context.frameInFlight) {
        updateAttachments(context);
        m_attachmentsFrame = context.frameInFlight;
    }

    return m_attachments;
}

SecondaryBufferInheritance RenderPass::getInheritance(const RenderPassContext &context)
{
    const RenderPassAttachments &attachments = getAttachments(context);

    SecondaryBufferInheritance inheritance;
    inheritance.colorFormats.reserve(attachments.colorAttachments.size());
    for (const RenderPassAttachment &colorAttachment : attachments.colorAttachments) {
        inheritance.colorFormats.push_back(colorAttachment.texture->getFormat());
    }

    if (attachments.depthAttachment.texture != nullptr) {
        inheritance.depthFormat = attachments.depthAttachment.texture->getFormat();
    }

    if (attachments.stencilAttachment.texture != nullptr) {
        inheritance.stencilFormat = attachments.stencilAttachment.texture->getFormat();
    }

    return inheritance;
}

void RenderPass::beginRendering(const RenderPassContext &context, CommandBuffer *primaryCb)
{
    const RenderPassAttachments &attachments = getAttachments(context);

    std::vector<VkImageMemoryBarrier> barriers;
    barriers.reserve(attachments.colorAttachments.size() + 2);
    for (const RenderPassAttachment &colorAttachment : attachments.colorAttachments) {
        s_appendTransitionBarrier(barriers, colorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }

    s_appendTransitionBarrier(barriers, attachments.depthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    // Depth and stencil may point at the same combined texture; do not barrier it twice.
    if (attachments.stencilAttachment.texture != attachments.depthAttachment.texture) {
        s_appendTransitionBarrier(barriers, attachments.stencilAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }

    // An attachment coming from a real layout was written by an earlier pass, so the barrier has to
    // wait on it. Nothing here knows which stage that was.
    // TODO: narrow once Texture also tracks the stage that last wrote each image
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    for (const VkImageMemoryBarrier &barrier : barriers) {
        if (barrier.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
        }
    }

    if (!barriers.empty()) {
        vkCmdPipelineBarrier(primaryCb->getCommandBufferVk(), srcStage,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                             nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    }

    std::vector<VkRenderingAttachmentInfo> colorAttachmentInfos;
    colorAttachmentInfos.reserve(attachments.colorAttachments.size());
    for (const RenderPassAttachment &colorAttachment : attachments.colorAttachments) {
        colorAttachmentInfos.push_back(s_buildColorAttachmentInfo(colorAttachment));
    }

    bool hasDepth = attachments.depthAttachment.texture != nullptr;
    bool hasStencil = attachments.stencilAttachment.texture != nullptr;

    VkRenderingAttachmentInfo depthAttachmentInfo{};
    if (hasDepth) {
        depthAttachmentInfo = s_buildDepthAttachmentInfo(attachments.depthAttachment);
    }

    VkRenderingAttachmentInfo stencilAttachmentInfo{};
    if (hasStencil) {
        stencilAttachmentInfo = s_buildStencilAttachmentInfo(attachments.stencilAttachment);
    }

    VkExtent2D renderExtent{0, 0};
    if (!attachments.colorAttachments.empty()) {
        const TextureSpecification &spec = attachments.colorAttachments[0].texture->getSpecification();
        renderExtent = {spec.width, spec.height};
    } else if (hasDepth) {
        const TextureSpecification &spec = attachments.depthAttachment.texture->getSpecification();
        renderExtent = {spec.width, spec.height};
    }

    RP_ASSERT(renderExtent.width != 0 && renderExtent.height != 0, "A pass must declare at least one attachment");

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = renderExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfos.size());
    renderingInfo.pColorAttachments = colorAttachmentInfos.data();
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachmentInfo : nullptr;
    renderingInfo.pStencilAttachment = hasStencil ? &stencilAttachmentInfo : nullptr;
    renderingInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

    vkCmdBeginRendering(primaryCb->getCommandBufferVk(), &renderingInfo);
}

void RenderPass::endRendering(CommandBuffer *primaryCb)
{
    vkCmdEndRendering(primaryCb->getCommandBufferVk());
}

} // namespace Rapture
