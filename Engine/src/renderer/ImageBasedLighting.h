#ifndef RAPTURE__IMAGE_BASED_LIGHTING_H
#define RAPTURE__IMAGE_BASED_LIGHTING_H

#include <atomic>
#include <cstdint>
#include <memory>

namespace Rapture {

class Texture;
class Shader;

/**
 * @brief Karis split-sum image-based lighting: diffuse irradiance cube, prefiltered specular cube, BRDF integration LUT
 *
 * Bakes from a source environment cubemap (the skybox) on the job system. The BRDF LUT is
 * scene- and view-independent so it bakes once; the two cubes rebake whenever the source changes.
 * Bindless handles are resolved lazily on the render thread and consumed by the lighting pass.
 */
class ImageBasedLighting {
  public:
    ImageBasedLighting();
    ~ImageBasedLighting();

    ImageBasedLighting(const ImageBasedLighting &) = delete;
    ImageBasedLighting &operator=(const ImageBasedLighting &) = delete;

    /**
     * @brief Kick an async bake of the irradiance and prefiltered cubes from a source environment cube
     * @param sourceCube Ready environment cubemap to convolve and prefilter
     */
    void bakeFromCube(Texture *sourceCube);

    bool isReady() const { return m_ready.load(std::memory_order_acquire); }

    uint32_t getIrradianceBindlessIndex();
    uint32_t getPrefilteredBindlessIndex();
    uint32_t getBrdfLutBindlessIndex();
    uint32_t getPrefilteredMipCount() const { return m_prefilteredMipCount; }

  private:
    void kickBakeIfIdle();

    std::unique_ptr<Shader> m_irradianceShader;
    std::unique_ptr<Shader> m_prefilterShader;
    std::unique_ptr<Shader> m_brdfShader;

    std::unique_ptr<Texture> m_irradianceCube;
    std::unique_ptr<Texture> m_prefilteredCube;
    std::unique_ptr<Texture> m_brdfLut;

    uint32_t m_prefilteredMipCount = 0;

    std::atomic<Texture *> m_requestedSource{nullptr};
    std::atomic<uint64_t> m_requestGen{0};
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_baking{false};
    bool m_brdfBaked = false;
};

} // namespace Rapture

#endif // RAPTURE__IMAGE_BASED_LIGHTING_H
