#include "renderer/passes/RenderPass.h"

#include "gpu/render_targets/SceneRenderTarget.h"
#include "gpu/textures/Texture.h"
#include "core/utils/rp_assert.h"

namespace Rapture {

static bool s_hasTarget(const RenderTargetRef &ref)
{
    return !std::holds_alternative<std::monostate>(ref);
}

static VkImageView s_attachmentView(const RenderTargetRef &ref)
{
    if (const auto *texture = std::get_if<Texture *>(&ref)) {
        return (*texture)->getAttachmentImageView();
    }
    if (const auto *image = std::get_if<RenderTargetImage>(&ref)) {
        return image->target->getImageView(image->index);
    }

    return VK_NULL_HANDLE;
}

static VkFormat s_attachmentFormat(const RenderTargetRef &ref)
{
    if (const auto *texture = std::get_if<Texture *>(&ref)) {
        return (*texture)->getFormat();
    }
    if (const auto *image = std::get_if<RenderTargetImage>(&ref)) {
        return image->target->getFormat();
    }

    return VK_FORMAT_UNDEFINED;
}

static VkExtent2D s_attachmentExtent(const RenderTargetRef &ref)
{
    if (const auto *texture = std::get_if<Texture *>(&ref)) {
        const TextureSpecification &spec = (*texture)->getSpecification();
        return {spec.width, spec.height};
    }
    if (const auto *image = std::get_if<RenderTargetImage>(&ref)) {
        return image->target->getExtent();
    }

    return {0, 0};
}

static VkImageLayout s_attachmentLayout(const RenderTargetRef &ref)
{
    if (const auto *texture = std::get_if<Texture *>(&ref)) {
        return (*texture)->getCurrentLayout();
    }
    if (const auto *image = std::get_if<RenderTargetImage>(&ref)) {
        return image->target->getImageLayout(image->index);
    }

    return VK_IMAGE_LAYOUT_UNDEFINED;
}

/**
 * @brief Barrier moving an attachment into a layout, recording the new layout on its owner
 */
static VkImageMemoryBarrier s_attachmentBarrier(const RenderTargetRef &ref, VkImageLayout oldLayout, VkImageLayout newLayout,
                                                VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask)
{
    if (const auto *texture = std::get_if<Texture *>(&ref)) {
        return (*texture)->getImageMemoryBarrier(oldLayout, newLayout, srcAccessMask, dstAccessMask);
    }

    const auto *image = std::get_if<RenderTargetImage>(&ref);
    if (image == nullptr) {
        RP_CORE_ERROR("Cannot barrier an attachment that references no image");
        return {};
    }
    image->target->setImageLayout(image->index, newLayout);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image->target->getImage(image->index);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    return barrier;
}

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
    info.imageView = s_attachmentView(attachment.target);
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
    info.imageView = s_attachmentView(attachment.target);
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
    info.imageView = s_attachmentView(attachment.target);
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
    if (!s_hasTarget(attachment.target)) {
        return;
    }

    // A cleared or discarded attachment has nothing worth preserving, so it transitions from
    // UNDEFINED. A LOAD attachment keeps its contents and so comes from the layout it tracks.
    const VkImageLayout oldLayout = attachment.loadOp == RenderPassAttachmentLoadOp::LOAD ? s_attachmentLayout(attachment.target)
                                                                                         : VK_IMAGE_LAYOUT_UNDEFINED;

    // Emitted even when the layout does not change, since it still carries the dependency on
    // whichever pass last wrote the attachment
    barriers.push_back(
        s_attachmentBarrier(attachment.target, oldLayout, newLayout, s_writeAccessForLayout(oldLayout), dstAccessMask));
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
        inheritance.colorFormats.push_back(s_attachmentFormat(colorAttachment.target));
    }

    if (s_hasTarget(attachments.depthAttachment.target)) {
        inheritance.depthFormat = s_attachmentFormat(attachments.depthAttachment.target);
    }

    if (s_hasTarget(attachments.stencilAttachment.target)) {
        inheritance.stencilFormat = s_attachmentFormat(attachments.stencilAttachment.target);
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
    if (attachments.stencilAttachment.target != attachments.depthAttachment.target) {
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

    bool hasDepth = s_hasTarget(attachments.depthAttachment.target);
    bool hasStencil = s_hasTarget(attachments.stencilAttachment.target);

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
        renderExtent = s_attachmentExtent(attachments.colorAttachments[0].target);
    } else if (hasDepth) {
        renderExtent = s_attachmentExtent(attachments.depthAttachment.target);
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
