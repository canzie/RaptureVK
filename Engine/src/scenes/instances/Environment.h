#ifndef RAPTURE__ENVIRONMENT_H
#define RAPTURE__ENVIRONMENT_H

#include "asset_manager/AssetCommon.h"
#include "asset_manager/AssetHandle.h"
#include "scenes/instances/SceneObject.h"

#include <glm/glm.hpp>

#include <memory>

namespace Rapture {

class ImageBasedLighting;
class ProceduralTexture;
class Texture;

struct AtmosphereSettings {
    float timeOfDay = 12.0f;
    float latitude = 0.0f;
    float longitude = 0.0f;

    glm::vec3 rayleigh = glm::vec3(5.8f, 13.5f, 33.1f);
    float mie = 21.0f;
    float sunIntensity = 20.0f;
    float mieG = 0.76f;
    float cameraAltitude = 1.0f;

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
     * @brief Regenerates the atmosphere skybox and its lighting when the settings or the sun moved
     */
    void update();

    AssetHandle skybox() const;
    void setSkybox(AssetHandle skybox);

    Texture *skyboxTexture() const { return m_skyboxTexture.get(); }

    float skyIntensity() const { return m_skyIntensity; }
    void setSkyIntensity(float skyIntensity) { m_skyIntensity = skyIntensity; }

    bool isSkyboxEnabled() const { return m_skyboxEnabled; }
    void setSkyboxEnabled(bool enabled) { m_skyboxEnabled = enabled; }

    bool usesAtmosphereSkybox() const { return m_usesAtmosphereSkybox; }
    void setUsesAtmosphereSkybox(bool usesAtmosphereSkybox) { m_usesAtmosphereSkybox = usesAtmosphereSkybox; }

    const AtmosphereSettings &atmosphere() const { return m_atmosphere; }
    AtmosphereSettings &atmosphere() { return m_atmosphere; }

    ImageBasedLighting *getImageBasedLighting() const { return m_ibl.get(); }

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
    bool ensureSkyboxGenerator();

    AssetPtr<Texture> m_skyboxTexture;
    float m_skyIntensity = 1.0f;
    bool m_skyboxEnabled = true;
    bool m_usesAtmosphereSkybox = false;

    AtmosphereSettings m_atmosphere;
    AtmosphereSettings m_lastApplied;

    std::unique_ptr<ProceduralTexture> m_skyboxGenerator;
    std::unique_ptr<ImageBasedLighting> m_ibl;
};

} // namespace Rapture

#endif // RAPTURE__ENVIRONMENT_H
