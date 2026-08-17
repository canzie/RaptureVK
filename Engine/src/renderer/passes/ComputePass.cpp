#include "ComputePass.h"

#include "gpu/command_buffers/CommandBuffer.h"
#include "core/utils/TracyProfiler.h"
#include "gpu/textures/Texture.h"

namespace Rapture {

// A storage image must be GENERAL, anything only sampled can stay in the read-only layout
static TextureUsage s_dispatchUsage(ComputeResourceAccess access)
{
    if (access == ComputeResourceAccess::READ) {
        return TEXTURE_USAGE_SAMPLED_COMPUTE;
    }
    return TEXTURE_USAGE_STORAGE_COMPUTE;
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

    std::vector<VkImageMemoryBarrier2> barriers;
    barriers.reserve(resources.size());

    for (const ComputeResource &resource : resources) {
        if (resource.texture == nullptr) {
            continue;
        }

        barriers.push_back(resource.texture->getBarrier2(s_dispatchUsage(resource.access), resource.discardContents));
    }

    if (!barriers.empty()) {
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependency.pImageMemoryBarriers = barriers.data();

        vkCmdPipelineBarrier2(commandBuffer->getCommandBufferVk(), &dependency);
    }

    record(context, commandBuffer);
}

} // namespace Rapture
