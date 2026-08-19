#ifndef RAPTURE__IMMEDIATE_SHAPES_DRAW_PASS_H
#define RAPTURE__IMMEDIATE_SHAPES_DRAW_PASS_H

#include "assets/asset_manager/AssetHandle.h"
#include "gpu/buffers/StorageBuffer.h"
#include "gpu/command_buffers/CommandBuffer.h"
#include "gpu/descriptors/DescriptorSet.h"
#include "gpu/pipelines/GraphicsPipeline.h"
#include "gpu/shaders/Shader.h"
#include "gpu/vulkan_context/RenderContext.h"
#include "renderer/ImmediateDrawList.h"
#include "renderer/passes/RenderPass.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Rapture {

/**
 * @brief Creation-time configuration for an immediate shapes draw pass and the buffers it uploads into
 */
struct ImmediateShapesDrawPassConfig {
    uint32_t width;
    uint32_t height;
    VkFormat colorFormat;
    VkFormat depthFormat;
    uint32_t framesInFlight;
};

/**
 * @brief Draws a draw list's lines and filled shapes over the rendered image
 */
class ImmediateShapesDrawPass : public RenderPass {
  public:
    ImmediateShapesDrawPass(const ImmediateShapesDrawPassConfig &config, const ImmediateDrawList *drawList);
    ~ImmediateShapesDrawPass();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    SecondaryBufferInheritance getInheritance(const RenderPassContext &context) override;
    void beginRendering(const RenderPassContext &context, CommandBuffer *primaryCb) override;

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    void createPipelines();

    /**
     * @brief Give one buffer of the ring a size, replacing whatever it held
     * @param slot Buffer to build
     * @param bytes Size to build it at
     */
    void buildBuffer(uint32_t slot, VkDeviceSize bytes);

    /**
     * @brief Bytes the draw list needs to hold every shape submitted this frame
     * @return The size, which no frame's buffer is guaranteed to have
     */
    VkDeviceSize requiredBytes() const;

    /**
     * @brief Settle what every frame's buffer should be sized at, from what this frame needs
     *
     * Grows the moment a frame does not fit, and reconsiders the size once a window has passed so a
     * single spike does not hold the memory forever.
     *
     * @param required Bytes this frame's shapes need
     */
    void updateTargetBytes(VkDeviceSize required);

    /**
     * @brief Where one depth mode's shapes sit within a buffer
     */
    struct DepthModeRange {
        uint32_t firstSegment = 0;
        uint32_t segmentCount = 0;
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
    };

    /**
     * @brief Copy the draw list into one buffer of the ring
     * @param slot Buffer to upload into
     * @param[out] ranges Where each depth mode landed
     * @param[out] vertexBase Index the first filled shape corner sits at
     */
    void uploadDrawList(uint32_t slot, std::array<DepthModeRange, DEPTH_MODE_COUNT> &ranges, uint32_t &vertexBase);

  private:
    const RenderContext *m_rc = nullptr;
    const ImmediateDrawList *m_drawList = nullptr;
    ImmediateShapesDrawPassConfig m_config;

    AssetPtr<Shader> m_segmentShader;
    AssetPtr<Shader> m_triangleShader;

    std::shared_ptr<GraphicsPipeline> m_segmentPipeline;
    std::shared_ptr<GraphicsPipeline> m_trianglePipeline;

    // One more buffer than there are frames in flight, so the one picked up was last written a full
    // cycle plus a frame ago and can be rebuilt in place
    std::vector<std::unique_ptr<StorageBuffer>> m_shapeBuffers;
    std::vector<std::unique_ptr<DescriptorSet>> m_shapeSets;
    std::vector<VkDeviceSize> m_bufferBytes;
    uint32_t m_currentSlot = 0;

    VkDeviceSize m_targetBytes = 0;
    VkDeviceSize m_windowPeakBytes = 0;
    uint32_t m_windowFrames = 0;

    float m_width;
    float m_height;
};

} // namespace Rapture

#endif // RAPTURE__IMMEDIATE_SHAPES_DRAW_PASS_H
