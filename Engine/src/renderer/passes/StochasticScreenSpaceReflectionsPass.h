#ifndef RAPTURE__STOCHASTIC_SCREEN_SPACE_REFLECTIONS_PASS_H
#define RAPTURE__STOCHASTIC_SCREEN_SPACE_REFLECTIONS_PASS_H

#include "asset_manager/AssetHandle.h"
#include "buffers/StorageBuffer.h"
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
 * @brief Screen-space reflections, from ray trace through to resolved radiance
 *
 * Traces one reflection ray per half-resolution pixel and stores where it landed, then shades those
 * hit points into full-resolution radiance. The hit record holds a location rather than colour, so
 * the resolve can shade a neighbour's hit against its own BRDF.
 */
class StochasticScreenSpaceReflectionsPass : public ComputePass {
  public:
    StochasticScreenSpaceReflectionsPass(uint32_t width, uint32_t height, uint32_t framesInFlight,
                                         std::vector<Texture *> sceneColorTextures);
    ~StochasticScreenSpaceReflectionsPass();

    void onResize(uint32_t width, uint32_t height) override;

    /**
     * @brief The hit records the trace wrote for a frame
     * @param frameInFlight Frame slot to read
     * @return The texture, or nullptr if the slot is out of range
     */
    Texture *getHitTexture(uint32_t frameInFlight) const;

    /**
     * @brief The resolved reflection radiance for a frame
     * @param frameInFlight Frame slot to read
     * @return The texture, or nullptr if the slot is out of range
     */
    Texture *getResolvedTexture(uint32_t frameInFlight) const;

    /**
     * @brief The temporally accumulated reflection for a frame, with confidence in alpha
     * @param frameInFlight Frame slot to read
     * @return The texture, or nullptr if the slot is out of range
     */
    Texture *getAccumulatedTexture(uint32_t frameInFlight) const;

    /**
     * @brief Averages the finished scene colour down its mip chain, one dispatch per mip
     *
     * Runs after the scene colour is complete rather than inside execute, so the frame that reads it
     * back as history finds it already filtered and in a sampleable layout. The chain is what lets
     * the resolve widen its lookup into a cone footprint instead of a point sample.
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordSceneColorMipChain(const RenderPassContext &context, CommandBuffer *commandBuffer);

    void setMaxDistance(float distance) { m_maxDistance = distance; }
    void setThickness(float thickness) { m_thickness = thickness; }
    void setStepCount(int32_t steps) { m_stepCount = steps; }

  protected:
    void record(const RenderPassContext &context, CommandBuffer *commandBuffer) override;
    void updateResources(const RenderPassContext &context) override;

  private:
    void loadShaders();
    void createTextures();
    void createDescriptorSets();

    /**
     * @brief Sets each tile's ray budget from the roughness it covers
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordClassify(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Compacts the tile budgets into a ray list and the indirect dispatch that consumes it
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordAllocate(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Marches a reflection ray per half-resolution pixel into the hit record
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordTrace(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Shades the hit records into full-resolution radiance
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordResolve(const RenderPassContext &context, CommandBuffer *commandBuffer);

    /**
     * @brief Accumulates the resolve into the previous frame's result along reflection depth
     * @param context The frame being rendered
     * @param commandBuffer The buffer to record into
     */
    void recordTemporal(const RenderPassContext &context, CommandBuffer *commandBuffer);

  private:
    const RenderContext *m_rc = nullptr;

    Shader *m_classifyShader = nullptr;
    Shader *m_allocateShader = nullptr;
    Shader *m_traceShader = nullptr;
    Shader *m_resolveShader = nullptr;
    Shader *m_downsampleShader = nullptr;
    Shader *m_temporalShader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<ComputePipeline> m_classifyPipeline;
    std::shared_ptr<ComputePipeline> m_allocatePipeline;
    std::shared_ptr<ComputePipeline> m_tracePipeline;
    std::shared_ptr<ComputePipeline> m_resolvePipeline;
    std::shared_ptr<ComputePipeline> m_downsamplePipeline;
    std::shared_ptr<ComputePipeline> m_temporalPipeline;

    std::vector<std::unique_ptr<Texture>> m_tileRayCountTextures;
    std::vector<std::unique_ptr<DescriptorSet>> m_tileRayCountSets;

    /// One entry per allocated ray, plus the VkDispatchIndirectCommand the trace is dispatched from
    std::vector<std::unique_ptr<StorageBuffer>> m_workItemBuffers;
    std::vector<std::unique_ptr<StorageBuffer>> m_indirectBuffers;
    std::vector<std::unique_ptr<DescriptorSet>> m_allocateSets;
    uint32_t m_maxWorkItems = 0;

    std::vector<std::unique_ptr<Texture>> m_hitTextures;
    std::vector<std::unique_ptr<Texture>> m_resolvedTextures;
    std::vector<std::unique_ptr<Texture>> m_accumulatedTextures;
    std::vector<std::unique_ptr<DescriptorSet>> m_hitSets;
    std::vector<std::unique_ptr<DescriptorSet>> m_resolvedSets;
    std::vector<std::unique_ptr<DescriptorSet>> m_accumulatedSets;

    std::vector<Texture *> m_sceneColorTextures;

    /// One storage-image set per mip of every scene colour target, indexed [target][mip]
    std::vector<std::vector<std::unique_ptr<DescriptorSet>>> m_sceneColorMipSets;

    uint32_t m_sceneColorMipLevels = 1;

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_halfWidth;
    uint32_t m_halfHeight;
    uint32_t m_tileCountX;
    uint32_t m_tileCountY;
    uint32_t m_framesInFlight;

    /// Rays per half-resolution pixel a tile may be allocated, by how rough it is
    int32_t m_minRays = 1;
    int32_t m_maxRays = 4;

    float m_maxDistance = 40.0f;
    float m_thickness = 0.5f;
    int32_t m_stepCount = 64;

    /// Weight of the previous frame's accumulation
    float m_hysteresis = 0.92f;

    bool m_hasAccumulatedHistory = false;
};

} // namespace Rapture

#endif // RAPTURE__STOCHASTIC_SCREEN_SPACE_REFLECTIONS_PASS_H
