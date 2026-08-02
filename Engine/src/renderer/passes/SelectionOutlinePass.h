#ifndef RAPTURE__SELECTION_OUTLINE_PASS_H
#define RAPTURE__SELECTION_OUTLINE_PASS_H

#include "asset_manager/AssetManager.h"
#include "buffers/command_buffers/CommandBuffer.h"
#include "buffers/descriptors/DescriptorSet.h"
#include "pipelines/GraphicsPipeline.h"
#include "renderer/passes/RenderPass.h"
#include "scenes/entities/Entity.h"
#include "shaders/Shader.h"
#include "window_context/vulkan_context/RenderContext.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Rapture {

class Texture;

/**
 * @brief Draws a border around the selected entity by edge detecting the G-buffer entity ids
 *
 * A pixel is border when the selection does not cover it but covers another within the thickness,
 * which follows the silhouette exactly and holds a constant width whatever the entity's size or
 * distance. Without the optional entity id slot there is nothing to edge detect and the pass draws
 * nothing.
 */
class SelectionOutlinePass : public RenderPass {
  public:
    SelectionOutlinePass(uint32_t framesInFlight, VkFormat colorFormat);
    ~SelectionOutlinePass();

    CommandBuffer *record(const RenderPassContext &context, const SecondaryBufferInheritance &inheritance) override;
    void onResize(uint32_t width, uint32_t height) override;

    /**
     * @brief Sets the colour the border is drawn in
     * @param color Border colour, in sRGB
     */
    void setOutlineColor(const glm::vec4 &color) { m_outlineColor = color; }

    /**
     * @brief Sets how far the border extends past the silhouette
     * @param thickness Border width in pixels
     */
    void setThickness(int32_t thickness) { m_thickness = thickness; }

  protected:
    void updateAttachments(const RenderPassContext &context) override;

  private:
    void createPipeline();

    /**
     * @brief The set holding the entity id texture for one frame, built the first time that frame is recorded
     * @param entityIdTexture The frame's entity id texture
     * @param frameInFlight Frame the set belongs to
     * @return The set, or nullptr when it could not be built
     */
    DescriptorSet *obtainEntityIdSet(Texture *entityIdTexture, uint32_t frameInFlight);

  private:
    const RenderContext *m_rc = nullptr;

    Shader *m_shader = nullptr;
    std::vector<AssetRef> m_shaderAssets;
    std::shared_ptr<GraphicsPipeline> m_pipeline;

    std::vector<std::unique_ptr<DescriptorSet>> m_entityIdSets;

    Entity m_selectedEntity;
    size_t m_entitySelectedListenerId = 0;
    size_t m_entityDeselectedListenerId = 0;

    glm::vec4 m_outlineColor = glm::vec4(1.0f, 0.45f, 0.0f, 1.0f);
    int32_t m_thickness = 2;

    float m_width = 0.0f;
    float m_height = 0.0f;
    VkFormat m_colorFormat;
};

} // namespace Rapture

#endif // RAPTURE__SELECTION_OUTLINE_PASS_H
