#ifndef RAPTURE__ENVIRONMENT_H
#define RAPTURE__ENVIRONMENT_H

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/assets/textures/ATexture.h>
#include <rapture/scene/instances/SceneObject.h>

#include <glm/glm.hpp>

#include <memory>

namespace Rapture {

class ImageBasedLighting;
class SkyAtmosphere;
class Texture;

struct AtmosphereSettings {
    float timeOfDay = 12.0f;
    float latitude = 0.0f;
    float longitude = 0.0f;

    glm::vec3 rayleighScattering = glm::vec3(5.802f, 13.558f, 33.1f); ///< 1e-6 per metre
    float rayleighScaleHeight = 8.0f; ///< km
    float mieScattering = 3.996f; ///< 1e-6 per metre
    float mieAbsorption = 4.4f; ///< 1e-6 per metre
    float mieScaleHeight = 1.2f; ///< km
    float mieG = 0.8f;
    float ozoneDensity = 1.0f;
    float groundAlbedo = 0.3f;
    float groundRadius = 6360.0f; ///< km
    float atmosphereThickness = 100.0f; ///< km
    float originAltitude = 0.0f; ///< metres above the ground
    bool tintsSunLight = true;

    bool operator==(const AtmosphereSettings &other) const = default;
};

/**
 * @brief The pair of atmosphere angles that place the sun in a direction
 */
struct SunAngles {
    float timeOfDay = 12.0f;
    float longitude = 0.0f;
};

/**
 * @brief The scene's sky and atmosphere, and the owner of the derived skybox and image based lighting.
 *
 * Placeless, so it derives from SceneObject rather than Node3D. A scene holds at most one, which is a
 * consequence of two of them being indistinguishable rather than a rule the tree enforces.
 */
class Environment : public SceneObject {
  public:
    Environment(Scene &scene, std::string_view name);
    ~Environment() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Regenerates the atmosphere, its skybox and its lighting when the settings or the sun changed
     */
    void update();

    AssetHandle skybox() const;
    void setSkybox(AssetHandle skybox);

    Texture *skyboxTexture() const { return m_skyboxTexture ? &m_skyboxTexture.get()->texture() : nullptr; }

    float skyIntensity() const { return m_skyIntensity; }
    void setSkyIntensity(float skyIntensity) { m_skyIntensity = skyIntensity; }

    bool isSkyboxEnabled() const { return m_skyboxEnabled; }
    void setSkyboxEnabled(bool enabled) { m_skyboxEnabled = enabled; }

    bool usesAtmosphereSkybox() const { return m_usesAtmosphereSkybox; }
    void setUsesAtmosphereSkybox(bool usesAtmosphereSkybox) { m_usesAtmosphereSkybox = usesAtmosphereSkybox; }

    const AtmosphereSettings &atmosphere() const { return m_atmosphere; }
    AtmosphereSettings &atmosphere() { return m_atmosphere; }

    ImageBasedLighting *getImageBasedLighting() const { return m_ibl.get(); }

    /**
     * @brief The atmosphere the sky is rendered from
     * @return The atmosphere, or nullptr while the sky does not use one
     */
    SkyAtmosphere *getSkyAtmosphere() const;

    static glm::vec3 sunDirection(float timeOfDay, float latitude, float longitude);

    /**
     * @brief Inverts sunDirection, recovering both angles so a hand placed sun keeps the direction it was given
     * @param sunDirection The direction to the sun, normalized
     * @param current The settings the result stays nearest to, resolving the morning and evening solutions
     * @return The angles, which sunDirection maps back onto the same direction
     */
    static SunAngles sunAnglesFromDirection(const glm::vec3 &sunDirection, const AtmosphereSettings &current);

    static float rayleighCoefficient(float wavelengthNm);
    static float wavelengthNm(float rayleighCoefficient);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    void ensureSkyAtmosphere();

    Ref<ATexture> m_skyboxTexture;
    float m_skyIntensity = 1.0f;
    bool m_skyboxEnabled = true;
    bool m_usesAtmosphereSkybox = false;

    AtmosphereSettings m_atmosphere;
    AtmosphereSettings m_lastApplied;

    std::unique_ptr<SkyAtmosphere> m_skyAtmosphere;
    std::unique_ptr<ImageBasedLighting> m_ibl;
};

} // namespace Rapture

#endif // RAPTURE__ENVIRONMENT_H
