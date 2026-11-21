#ifndef RAPTURE__VIEW_ATMOSPHERE_LOOKUP_TABLES_PASS_H
#define RAPTURE__VIEW_ATMOSPHERE_LOOKUP_TABLES_PASS_H

#include "renderer/atmosphere/SkyAtmosphere.h"
#include "renderer/passes/ComputePass.h"

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/shaders/AShader.h>
#include <rapture/gpu/buffers/UniformBuffer.h>
#include <rapture/gpu/descriptors/DescriptorSet.h>
#include <rapture/gpu/pipelines/ComputePipeline.h>
#include <rapture/gpu/textures/Texture.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Rapture {

struct RenderContext;

/**
 * @brief The atmosphere as one camera sees it: the sky around the camera and the aerial perspective volume
 * over its view frustum
 */
class ViewAtmosphereLookupTablesPass : public ComputePass {
  public:
    explicit ViewAtmosphereLookupTablesPass(uint32_t framesInFlight);
    ~ViewAtmosphereLookupTablesPass();

    void onResize(uint32_t width, uint32_t height) override;

    /**
     * @brief Selects the atmosphere and camera this frame's tables are built for
     * @param context The frame being rendered
     * @param atmosphere The atmosphere, or nullptr when the view has none
     * @return Whether there is an atmosphere to draw this frame
     */
    bool prepare(const RenderPassContext &context, const SkyAtmosphere *atmosphere);

    const SkyAtmosphere *getAtmosphere() const { return m_atmosphere; }
    Texture *getSkyViewLut() const { return m_skyViewLut.get(); }
    Texture *getAerialPerspectiveVolume() const { return m_aerialPerspectiveVolume.get(); }

    /**
     * @brief The set binding the atmosphere uniforms for a frame, alone in set 4
     * @param frameInFlight The frame slot
     * @return The descriptor set
     */
    DescriptorSet *getUniformSet(uint32_t frameInFlight) const { return m_uniformSets[frameInFlight].get(); }

    /**
     * @brief The camera's position in world metres
     * @return The position
     */
    const glm::vec3 &getCameraPosition() const { return m_cameraPosition; }

    /**
     * @brief The altitude the sky view lookup table was built for
     * @return The altitude in km
     */
    float getSkyViewAltitude() const { return m_skyViewAltitude; }

  protected:
    void record(const RenderPassContext &context, CommandBuffer *commandBuffer) override;
    void updateResources(const RenderPassContext &context) override;

  private:
    void loadShaders();
    void createTextures();
    void createDescriptorSets();

  private:
    const RenderContext *m_rc = nullptr;
    uint32_t m_framesInFlight;

    Ref<AShader> m_skyViewShader;
    Ref<AShader> m_aerialPerspectiveShader;
    std::shared_ptr<ComputePipeline> m_skyViewPipeline;
    std::shared_ptr<ComputePipeline> m_aerialPerspectivePipeline;

    std::unique_ptr<Texture> m_skyViewLut;
    std::unique_ptr<Texture> m_aerialPerspectiveVolume;

    std::vector<std::unique_ptr<UniformBuffer>> m_uniformBuffers;
    std::vector<std::unique_ptr<DescriptorSet>> m_skyViewSets;
    std::vector<std::unique_ptr<DescriptorSet>> m_aerialPerspectiveSets;
    std::vector<std::unique_ptr<DescriptorSet>> m_uniformSets;

    const SkyAtmosphere *m_atmosphere = nullptr;
    glm::vec3 m_cameraPosition = glm::vec3(0.0f);
    uint32_t m_cameraSlot = 0;

    AtmosphereUniforms m_skyViewUniforms{};
    float m_skyViewAltitude = -1.0f;
    bool m_skyViewStale = true;
};

} // namespace Rapture

#endif // RAPTURE__VIEW_ATMOSPHERE_LOOKUP_TABLES_PASS_H
