#ifndef RAPTURE__HI_Z_PASS_H
#define RAPTURE__HI_Z_PASS_H

#include "asset_manager/AssetHandle.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "pipelines/ComputePipeline.h"
#include "renderer/passes/ComputePass.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"

#include <memory>
#include <vector>

namespace Rapture {

struct RenderContext;

/**
 * @brief Builds a min-Z pyramid over linear view depth
 *
 * Mip 0 linearizes the G-buffer depth, every mip below it takes the minimum of its parents so a
 * level bounds the nearest surface of everything under it. Allocated per frame in flight, since
 * mip 0 doubles as the previous frame's linear depth for temporal reprojection.
 */
class HiZPass : public ComputePass {
  public:
    HiZPass(uint32_t width, uint32_t height, uint32_t framesInFlight);
    ~HiZPass();

    void onResize(uint32_t width, uint32_t height) override;

    /**
     * @brief The pyramid written for a frame
     * @param frameInFlight Frame slot to read
     * @return The texture, or nullptr if the slot is out of range
     */
    Texture *getHiZTexture(uint32_t frameInFlight) const;

    uint32_t getMipLevels() const { return m_mipLevels; }

  protected:
    void record(const RenderPassContext &context, CommandBuffer *commandBuffer) override;
    void updateResources(const RenderPassContext &context) override;

  private:
    void loadShaders();
    void createTextures();
    void createDescriptorSets();

    /**
     * @brief Writes mip 0 as linear view depth
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordLinearize(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Reduces mip 0 down to the 1x1 level, one dispatch per mip
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordReduce(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Makes a just-written mip visible to the dispatch that reduces it
     * @param commandBuffer The buffer to record into
     * @param texture The pyramid being built
     * @param mip Mip level that was written
     */
    static void barrierMip(CommandBuffer *commandBuffer, Texture *texture, uint32_t mip);

  private:
    const RenderContext *m_rc = nullptr;

    Shader *m_linearizeShader = nullptr;
    Shader *m_reduceShader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<ComputePipeline> m_linearizePipeline;
    std::shared_ptr<ComputePipeline> m_reducePipeline;

    std::vector<std::unique_ptr<Texture>> m_hiZTextures;

    /// One storage-image set per mip of every frame's pyramid, indexed [frameInFlight][mip]
    std::vector<std::vector<std::unique_ptr<DescriptorSet>>> m_mipSets;

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_framesInFlight;
    uint32_t m_mipLevels = 1;
};

} // namespace Rapture

#endif // RAPTURE__HI_Z_PASS_H
