#ifndef RAPTURE__COMPUTE_PASS_H
#define RAPTURE__COMPUTE_PASS_H

#include "renderer/passes/RenderPassContext.h"

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

class CommandBuffer;
class Texture;

/**
 * @brief How a dispatch touches a resource, which selects the layout it is transitioned into
 */
enum class ComputeResourceAccess {
    READ,
    WRITE,
    READ_WRITE
};

/**
 * @brief One texture a dispatch reads or writes, and what the pass must do to make it usable
 */
struct ComputeResource {
    Texture *texture = nullptr;
    ComputeResourceAccess access = ComputeResourceAccess::READ;

    /// Discard the contents instead of transitioning from the layout the texture tracks, for an
    /// output that is fully overwritten
    bool discardContents = false;
};

/**
 * @brief A compute dispatch, with the barrier envelope owned by the base
 *
 * The sibling of RenderPass for work with no attachments. A subclass declares the resources it
 * touches and records the dispatch itself, exactly as a RenderPass declares attachments and records
 * draws.
 */
class ComputePass {
  public:
    virtual ~ComputePass() = default;

    /**
     * @brief Transitions the declared resources and records the dispatch
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void execute(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief The resources this pass touches, rebuilt only when the frame in flight changes
     * @param context The frame being rendered
     * @return The cached resource list
     */
    const std::vector<ComputeResource> &getResources(const RenderPassContext &context);

    /**
     * @brief Resizes any resources the pass owns
     * @param width New width in pixels
     * @param height New height in pixels
     */
    virtual void onResize(uint32_t width, uint32_t height) = 0;

  protected:
    /**
     * @brief Binds the pipeline and issues the dispatch
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    virtual void record(const RenderPassContext &context, CommandBuffer *commandBuffer) = 0;

    /**
     * @brief Fills m_resources for the given frame
     * @param context The frame being rendered
     */
    virtual void updateResources(const RenderPassContext &context) = 0;

    /**
     * @brief Forces the resource list to rebuild on the next getResources
     */
    void invalidateResources() { m_resourcesFrame = UINT32_MAX; }

    /**
     * @brief The number of work groups covering a thread count
     * @param threads Total threads needed along the axis
     * @param localSize The shader's local_size along the same axis
     * @return The group count, rounded up
     */
    static uint32_t groupCount(uint32_t threads, uint32_t localSize);

  protected:
    std::vector<ComputeResource> m_resources;
    uint32_t m_resourcesFrame = UINT32_MAX;
};

} // namespace Rapture

#endif // RAPTURE__COMPUTE_PASS_H
