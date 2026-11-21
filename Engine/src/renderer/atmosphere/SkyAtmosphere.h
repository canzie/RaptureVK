#ifndef RAPTURE__SKY_ATMOSPHERE_H
#define RAPTURE__SKY_ATMOSPHERE_H

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/shaders/AShader.h>
#include <rapture/assets/textures/ATexture.h>
#include <rapture/gpu/buffers/UniformBuffer.h>
#include <rapture/gpu/command_buffers/CommandBuffer.h>
#include <rapture/gpu/descriptors/DescriptorSet.h>
#include <rapture/gpu/pipelines/ComputePipeline.h>
#include <rapture/gpu/textures/Texture.h>

#include <glm/glm.hpp>

#include <memory>

namespace Rapture {

struct AtmosphereSettings;

/**
 * @brief The atmosphere uniform block every atmosphere shader reads, see atmosphere/AtmosphereCommon.glsl
 */
struct AtmosphereUniforms {
    alignas(16) glm::vec4 rayleighScattering; ///< xyz per km, w = density exponent scale
    alignas(16) glm::vec4 mieScattering;      ///< xyz per km, w = density exponent scale
    alignas(16) glm::vec4 mieExtinction;      ///< xyz per km, w = phase asymmetry
    alignas(16) glm::vec4 ozoneAbsorption;    ///< xyz per km
    alignas(16) glm::vec4 groundAlbedo;       ///< w = ground radius in km
    alignas(16) glm::vec4 sunDirection;       ///< w = atmosphere thickness in km
    alignas(16) glm::vec4 sunIlluminance;     ///< w = cosine of the sun disk's angular radius
    alignas(4) uint32_t transmittanceLut;
    alignas(4) uint32_t multipleScatteringLut;
    alignas(4) float originAltitude; ///< km
    alignas(4) float unused;

    bool operator==(const AtmosphereUniforms &other) const = default;
};

static_assert(sizeof(AtmosphereUniforms) == 128, "AtmosphereUniforms must match the std140 block");

/**
 * @brief A planet's atmosphere: its transmittance and multiple scattering lookup tables, and the environment
 * cubemap of the sky they light
 */
class SkyAtmosphere {
  public:
    SkyAtmosphere();
    ~SkyAtmosphere();

    SkyAtmosphere(const SkyAtmosphere &) = delete;
    SkyAtmosphere &operator=(const SkyAtmosphere &) = delete;

    /**
     * @brief Rebuilds the lookup tables and the environment cubemap when the atmosphere or its sun changed
     * @param settings The atmosphere
     * @param sunDirection The direction towards the sun, normalized
     * @param sunIlluminance The sun's illuminance at the top of the atmosphere
     * @return Whether anything was rebuilt
     */
    bool update(const AtmosphereSettings &settings, const glm::vec3 &sunDirection, const glm::vec3 &sunIlluminance);

    bool isValid() const { return m_valid; }

    /**
     * @brief The uniforms the lookup tables were last built with
     * @return The uniforms, with the lookup tables' bindless indices filled in
     */
    const AtmosphereUniforms &getUniforms() const { return m_uniforms; }

    Texture *getTransmittanceLut() const { return m_transmittanceLut.get(); }
    Texture *getMultipleScatteringLut() const { return m_multipleScatteringLut.get(); }
    Ref<ATexture> getEnvironmentCubemap() const { return m_environmentCubemap; }

    /**
     * @brief The fraction of sunlight that crosses the atmosphere down to the world origin
     * @param settings The atmosphere
     * @param sunDirection The direction towards the sun, normalized
     * @return The transmittance per channel, zero once the sun is below the horizon
     */
    static glm::vec3 transmittanceToSun(const AtmosphereSettings &settings, const glm::vec3 &sunDirection);

  private:
    bool loadShaders();
    void createTextures();
    void createDescriptorSets();
    void recordBake(CommandBuffer *commandBuffer);

    /**
     * @brief Converts the authored settings into the shader uniforms
     * @param settings The atmosphere
     * @param sunDirection The direction towards the sun, normalized
     * @param sunIlluminance The sun's illuminance at the top of the atmosphere
     * @return The uniforms, lookup table indices included
     */
    AtmosphereUniforms buildUniforms(const AtmosphereSettings &settings, const glm::vec3 &sunDirection,
                                     const glm::vec3 &sunIlluminance);

  private:
    Ref<AShader> m_transmittanceShader;
    Ref<AShader> m_multipleScatteringShader;
    Ref<AShader> m_environmentCubemapShader;

    std::shared_ptr<ComputePipeline> m_transmittancePipeline;
    std::shared_ptr<ComputePipeline> m_multipleScatteringPipeline;
    std::shared_ptr<ComputePipeline> m_environmentCubemapPipeline;

    std::unique_ptr<Texture> m_transmittanceLut;
    std::unique_ptr<Texture> m_multipleScatteringLut;
    Ref<ATexture> m_environmentCubemap;

    std::unique_ptr<UniformBuffer> m_uniformBuffer;
    std::unique_ptr<DescriptorSet> m_transmittanceSet;
    std::unique_ptr<DescriptorSet> m_multipleScatteringSet;
    std::unique_ptr<DescriptorSet> m_environmentCubemapSet;

    AtmosphereUniforms m_uniforms{};
    bool m_built = false;
    bool m_valid = false;
};

} // namespace Rapture

#endif // RAPTURE__SKY_ATMOSPHERE_H
