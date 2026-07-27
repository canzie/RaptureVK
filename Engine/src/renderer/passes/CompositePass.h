#ifndef RAPTURE__COMPOSITE_PASS_H
#define RAPTURE__COMPOSITE_PASS_H

#include "buffers/command_buffers/CommandBuffer.h"
#include "pipelines/GraphicsPipeline.h"
#include "renderer/passes/RenderPass.h"
#include "shaders/Shader.h"
#include "window_context/vulkan_context/RenderContext.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Rapture {

class Texture;

/**
 * @brief Tone maps the linear HDR scene colour into the presented render target
 *
 * The only pass that writes a swapchain image, which SceneRenderTarget exposes as a raw VkImage
 * rather than a Texture. It therefore overrides beginRendering and endRendering instead of
 * declaring attachments, and updateAttachments stays empty.
 */
class CompositePass : public RenderPass {
  public:
    CompositePass(float width, float height, VkFormat colorFormat);
    ~CompositePass();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    SecondaryBufferInheritance getInheritance(const RenderPassContext &context) override;
    void beginRendering(const RenderPassContext &context, CommandBuffer *primaryCb) override;
    void endRendering(CommandBuffer *primaryCb) override;

    /**
     * @brief Set the exposure applied before tone mapping
     * @param stops Exposure in f-stops
     */
    void setExposureStops(float stops) { m_exposureStops = stops; }

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    void createPipeline();

    /**
     * @brief Transition the presented image into a colour attachment layout and the scene colour into a sampleable one
     * @param primaryCb Primary buffer the rendering is issued on
     * @param targetImage Image being written this frame
     * @param sceneColor HDR scene colour this pass samples
     */
    void setupMemoryBarriers(CommandBuffer *primaryCb, VkImage targetImage, Texture *sceneColor);

  private:
    const RenderContext *m_rc = nullptr;
    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;

    std::shared_ptr<GraphicsPipeline> m_pipeline;

    VkFormat m_colorFormat;
    float m_width;
    float m_height;
    float m_exposureStops = 1.0f;
};

} // namespace Rapture

#endif // RAPTURE__COMPOSITE_PASS_H
