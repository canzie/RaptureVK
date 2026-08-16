#include "ComputePass.h"

#include "gpu/command_buffers/CommandBuffer.h"
#include "core/utils/TracyProfiler.h"
#include "gpu/textures/Texture.h"

namespace Rapture {

// A storage image must be GENERAL, anything only sampled can stay in the read-only layout
static VkImageLayout s_dispatchLayout(ComputeResourceAccess access)
{
    if (access == ComputeResourceAccess::READ) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_GENERAL;
}

static VkAccessFlags s_dispatchAccess(ComputeResourceAccess access)
{
    switch (access) {
    case ComputeResourceAccess::READ:
        return VK_ACCESS_SHADER_READ_BIT;
    case ComputeResourceAccess::WRITE:
        return VK_ACCESS_SHADER_WRITE_BIT;
    case ComputeResourceAccess::READ_WRITE:
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    return VK_ACCESS_SHADER_READ_BIT;
}

uint32_t ComputePass::groupCount(uint32_t threads, uint32_t localSize)
{
    if (localSize == 0) {
        return 0;
    }
    return (threads + localSize - 1) / localSize;
}

const std::vector<ComputeResource> &ComputePass::getResources(const RenderPassContext &context)
{
    if (m_resourcesFrame != context.frameInFlight) {
        updateResources(context);
        m_resourcesFrame = context.frameInFlight;
    }
    return m_resources;
}

void ComputePass::execute(const RenderPassContext &context, CommandBuffer *commandBuffer)
{
    RAPTURE_PROFILE_FUNCTION();

    const std::vector<ComputeResource> &resources = getResources(context);

    std::vector<VkImageMemoryBarrier> barriers;
    barriers.reserve(resources.size());

    // A resource entering from UNDEFINED has no prior contents to wait on, so only one that carries
    // real data forces a dependency on the work that produced it
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    for (const ComputeResource &resource : resources) {
        if (resource.texture == nullptr) {
            continue;
        }

        const VkImageLayout target = s_dispatchLayout(resource.access);
        const VkImageLayout current = resource.discardContents ? VK_IMAGE_LAYOUT_UNDEFINED : resource.texture->getCurrentLayout();

        if (current != VK_IMAGE_LAYOUT_UNDEFINED) {
            // TODO: conservative until Texture also tracks the stage that last wrote it
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        if (current == target) {
            continue;
        }

        barriers.push_back(resource.texture->getImageMemoryBarrier(current, target, 0, s_dispatchAccess(resource.access)));
    }

    if (!barriers.empty()) {
        vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    }

    record(context, commandBuffer);

    barriers.clear();
    for (const ComputeResource &resource : resources) {
        if (resource.texture == nullptr || !resource.readableAfter) {
            continue;
        }

        barriers.push_back(resource.texture->getImageMemoryBarrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                   s_dispatchAccess(resource.access), VK_ACCESS_SHADER_READ_BIT));
    }

    if (!barriers.empty()) {
        vkCmdPipelineBarrier(commandBuffer->getCommandBufferVk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()),
                             barriers.data());
    }
}

} // namespace Rapture
