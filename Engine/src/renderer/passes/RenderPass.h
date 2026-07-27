#ifndef RAPTURE__RENDER_PASS_H
#define RAPTURE__RENDER_PASS_H

#include "buffers/command_buffers/CommandBuffer.h"
#include "renderer/passes/RenderPassContext.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Rapture {

class Texture;

/**
 * @brief What happens to an attachment's existing contents when rendering begins
 */
enum class RenderPassAttachmentLoadOp {
    LOAD,
    CLEAR,
    DONT_CARE
};

/**
 * @brief Whether an attachment's contents survive once rendering ends
 */
enum class RenderPassAttachmentStoreOp {
    STORE,
    DONT_CARE
};

/**
 * @brief One attachment a pass renders into
 *
 * The clear fields are read only when loadOp is CLEAR, colour attachments taking clearColor and
 * depth/stencil attachments taking clearDepth and clearStencil.
 */
struct RenderPassAttachment {
    Texture *texture = nullptr;
    RenderPassAttachmentLoadOp loadOp = RenderPassAttachmentLoadOp::LOAD;
    RenderPassAttachmentStoreOp storeOp = RenderPassAttachmentStoreOp::STORE;
    glm::vec4 clearColor = glm::vec4(0.0f);
    float clearDepth = 1.0f;
    uint32_t clearStencil = 0;
};

/**
 * @brief The set of attachments a pass renders into for one frame
 *
 * A depth or stencil attachment with a null texture is absent.
 */
struct RenderPassAttachments {
    std::vector<RenderPassAttachment> colorAttachments;
    RenderPassAttachment depthAttachment;
    RenderPassAttachment stencilAttachment;
};

/**
 * @brief A pass that renders into attachments through a secondary command buffer
 *
 * Ordering between passes stays with the renderer, which calls beginRendering, replays the
 * recorded secondary, and calls endRendering for each pass in sequence.
 */
class RenderPass {
  public:
    virtual ~RenderPass() = default;

    /**
     * @brief Refresh and return the attachments this pass renders into
     * @param context Per-frame inputs
     * @return The pass's colour, depth and stencil attachments
     */
    const RenderPassAttachments &getAttachments(const RenderPassContext &context);

    /**
     * @brief Record the pass body into a secondary command buffer
     *
     * Safe to call from a job, and called for every pass before any of them is replayed.
     *
     * @param context Per-frame inputs
     * @param inheritance Attachment formats the secondary buffer inherits
     * @return The recorded secondary buffer, or nullptr when the pass has nothing to draw
     */
    virtual CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) = 0;

    /**
     * @brief Recreate size-dependent resources
     * @param width New target width
     * @param height New target height
     */
    virtual void onResize(uint32_t width, uint32_t height) = 0;

    /**
     * @brief The inheritance a secondary buffer for this pass must be recorded against
     * @param context Per-frame inputs
     * @return Inheritance carrying this pass's colour, depth and stencil formats
     */
    virtual SecondaryBufferInheritance getInheritance(const RenderPassContext &context);

    /**
     * @brief Transition this pass's attachments and begin dynamic rendering
     * @param context Per-frame inputs
     * @param primaryCb Primary buffer the rendering is issued on
     */
    virtual void beginRendering(const RenderPassContext &context, CommandBuffer *primaryCb);

    /**
     * @brief End dynamic rendering
     * @param primaryCb Primary buffer the rendering was issued on
     */
    virtual void endRendering(CommandBuffer *primaryCb);

  protected:
    /**
     * @brief Fill m_attachments with the attachments this pass renders into this frame
     *
     * Attachments vary per frame, since passes select per-frame-in-flight textures and render
     * target images by index. Clear and refill rather than reassigning, so the vector keeps its
     * capacity across frames.
     *
     * @param context Per-frame inputs
     */
    virtual void updateAttachments(const RenderPassContext &context) = 0;

    /**
     * @brief Force the next getAttachments to refill, after the attachment textures are recreated
     */
    void invalidateAttachments() { m_attachmentsFrame = UINT32_MAX; }

  protected:
    RenderPassAttachments m_attachments;
    uint32_t m_attachmentsFrame = UINT32_MAX; ///< frame in flight m_attachments was last filled for
};

} // namespace Rapture

#endif // RAPTURE__RENDER_PASS_H
