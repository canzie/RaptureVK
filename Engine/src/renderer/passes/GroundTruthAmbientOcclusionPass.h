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
 * @brief Screen-space ambient occlusion from the depth buffer
 */
class GroundTruthAmbientOcclusionPass : public ComputePass {
  public:
    GroundTruthAmbientOcclusionPass(uint32_t width, uint32_t height, uint32_t framesInFlight);
    ~GroundTruthAmbientOcclusionPass();

    void onResize(uint32_t width, uint32_t height) override;

    /**
     * @brief The occlusion written for a frame
     * @param frameInFlight Frame slot to read
     * @return The texture, or nullptr if the slot is out of range
     */
    Texture *getOcclusionTexture(uint32_t frameInFlight) const;

  protected:
    void record(const RenderPassContext &context, CommandBuffer *commandBuffer) override;
    void updateResources(const RenderPassContext &context) override;

  private:
    void loadShaders();
    void createTextures();
    void createDescriptorSets();

  private:
    const RenderContext *m_rc = nullptr;

    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<ComputePipeline> m_pipeline;

    std::vector<std::unique_ptr<Texture>> m_occlusionTextures;
    std::vector<std::unique_ptr<DescriptorSet>> m_occlusionSets;

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_framesInFlight;
};

} // namespace Rapture

#endif // RAPTURE__GROUND_TRUTH_AMBIENT_OCCLUSION_PASS_H
