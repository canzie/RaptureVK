#ifndef RAPTURE__GROUND_TRUTH_AMBIENT_OCCLUSION_PASS_H
#define RAPTURE__GROUND_TRUTH_AMBIENT_OCCLUSION_PASS_H

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
 * @brief Ground truth ambient occlusion, from horizon search through to a filtered result
 *
 * Sweeps a few screen-space slices per pixel, finds the horizon each one closes to, and integrates
 * the visible arc in closed form. Alongside the occlusion it produces a bent normal, the direction
 * the surface is open towards, which a consumer needs to occlude anything other than a uniformly
 * distributed lobe.
 */
class GroundTruthAmbientOcclusionPass : public ComputePass {
  public:
    GroundTruthAmbientOcclusionPass(uint32_t width, uint32_t height, uint32_t framesInFlight);
    ~GroundTruthAmbientOcclusionPass();

    void onResize(uint32_t width, uint32_t height) override;

    /**
     * @brief The raw horizon search result for a frame, before either filter
     * @param frameInFlight Frame slot to read
     * @return The texture, or nullptr if the slot is out of range
     */
    Texture *getOcclusionTexture(uint32_t frameInFlight) const;

    /**
     * @brief The filtered occlusion for a frame, with the bent normal in rgb and visibility in alpha
     * @param frameInFlight Frame slot to read
     * @return The texture, or nullptr if the slot is out of range
     */
    Texture *getDenoisedTexture(uint32_t frameInFlight) const;

    void setRadius(float radius) { m_radius = radius; }
    void setSliceCount(int32_t slices) { m_sliceCount = slices; }
    void setStepCount(int32_t steps) { m_stepCount = steps; }

  protected:
    void record(const RenderPassContext &context, CommandBuffer *commandBuffer) override;
    void updateResources(const RenderPassContext &context) override;

  private:
    void loadShaders();
    void createTextures();
    void createDescriptorSets();

    /**
     * @brief Searches the horizon along each slice and integrates the arc it leaves open
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordOcclusion(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Averages the slices neighbouring pixels traced, then accumulates along the motion vector
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordDenoise(const RenderPassContext &context, CommandBuffer *commandBuffer);

  private:
    const RenderContext *m_rc = nullptr;

    Shader *m_occlusionShader = nullptr;
    Shader *m_denoiseShader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<ComputePipeline> m_occlusionPipeline;
    std::shared_ptr<ComputePipeline> m_denoisePipeline;

    std::vector<std::unique_ptr<Texture>> m_occlusionTextures;
    std::vector<std::unique_ptr<Texture>> m_denoisedTextures;
    std::vector<std::unique_ptr<DescriptorSet>> m_occlusionSets;
    std::vector<std::unique_ptr<DescriptorSet>> m_denoisedSets;

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_framesInFlight;

    /// How far from a surface geometry still occludes it, in world units
    float m_radius = 1.0f;

    /// Cap on what that radius may cover in pixels, so a surface against the camera stays bounded
    float m_maxScreenRadius = 128.0f;

    /// Fraction of the radius the falloff to no contribution is spread over
    float m_falloffRange = 0.25f;

    int32_t m_sliceCount = 3;
    int32_t m_stepCount = 4;

    /// Depth difference, relative to the depth itself, that separates one surface from another
    float m_depthRejection = 0.05f;

    /// Weight of the previous frame's accumulation
    float m_hysteresis = 0.9f;

    bool m_hasHistory = false;
};

} // namespace Rapture

#endif // RAPTURE__GROUND_TRUTH_AMBIENT_OCCLUSION_PASS_H
