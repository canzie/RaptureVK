#ifndef RAPTURE__SKY_AND_AERIAL_PERSPECTIVE_PASS_H
#define RAPTURE__SKY_AND_AERIAL_PERSPECTIVE_PASS_H

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/shaders/AShader.h>
#include <rapture/gpu/pipelines/GraphicsPipeline.h>
#include <rapture/renderer/passes/RenderPass.h>

#include <memory>

namespace Rapture {

struct RenderContext;
class ViewAtmosphereLookupTablesPass;

/**
 * @brief Draws the atmosphere's sky behind the lit scene and the atmosphere between the camera and the scene
 */
class SkyAndAerialPerspectivePass : public RenderPass {
  public:
    /**
     * @brief Creates the pass
     * @param colorFormat The format of the scene colour it draws into
     * @param lookupTables The view's atmosphere tables it reads, which outlive the pass
     */
    SkyAndAerialPerspectivePass(VkFormat colorFormat, const ViewAtmosphereLookupTablesPass &lookupTables);
    ~SkyAndAerialPerspectivePass();

    CommandBufferLease record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    /**
     * @brief Scales the sky and the light the atmosphere scatters towards the camera
     * @param intensity The multiplier
     */
    void setSkyIntensity(float intensity) { m_skyIntensity = intensity; }

  protected:
    void updateAttachments(const RenderPassContext &context) override;
    void fillInputs(const RenderPassContext &context) override;

  private:
    void createPipeline();

  private:
    const RenderContext *m_rc = nullptr;
    const ViewAtmosphereLookupTablesPass &m_lookupTables;

    Ref<AShader> m_shader;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    VkFormat m_colorFormat;
    float m_skyIntensity = 1.0f;
};

} // namespace Rapture

#endif // RAPTURE__SKY_AND_AERIAL_PERSPECTIVE_PASS_H
