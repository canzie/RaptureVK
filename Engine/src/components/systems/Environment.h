#ifndef RAPTURE__ENVIRONMENT_H
#define RAPTURE__ENVIRONMENT_H

#include "components/Components.h"
#include "scenes/entities/Entity.h"

#include <glm/glm.hpp>
#include <memory>

namespace Rapture {

class ProceduralTexture;

class Environment {
  public:
    explicit Environment(Entity environment);
    ~Environment();

    void update();

    Entity getEntity() const { return m_entity; }

    static glm::vec3 sunDirection(float timeOfDay, float latitude, float longitude);
    static float timeOfDayFromSun(const glm::vec3 &sunDirection, float latitude);

    static float rayleighCoefficient(float wavelengthNm);
    static float wavelengthNm(float rayleighCoefficient);

  private:
    bool ensureSkyboxGenerator(SkyboxComponent &sky);

    Entity m_entity;
    std::unique_ptr<ProceduralTexture> m_skyboxGenerator;

    AtmosphereComponent m_lastApplied{};
};

} // namespace Rapture

#endif // RAPTURE__ENVIRONMENT_H
