#ifndef RAPTURE__INDIRECTLIGHTINGCOMPONENT_H
#define RAPTURE__INDIRECTLIGHTINGCOMPONENT_H

#include <glm/glm.hpp>
#include <variant>

namespace Rapture {

struct AmbientSettings {
    glm::vec3 ambientColor = glm::vec3(0.03f);

    AmbientSettings() = default;
    explicit AmbientSettings(glm::vec3 color) : ambientColor(color) {}
};

// Settings for DDGI (Dynamic Diffuse Global Illumination)
// The DDGI system will maintain its own internal state and use these as hints
struct DDGISettings {
    glm::uvec3 probeCount = glm::uvec3(16);
    glm::vec3 probeSpacing = glm::vec3(2.0f);
    glm::vec3 gridOrigin = glm::vec3(0.0f);
    uint32_t raysPerProbe = 256;
    float intensity = 1.0f;

    bool visualizeProbes = false;

    DDGISettings() = default;
};

// General indirect lighting component
// Holds settings for global illumination techniques
struct IndirectLightingComponent {
    float giIntensity = 1.0f;

    bool enabled = true;

    std::variant<std::monostate, AmbientSettings, DDGISettings> technique;

    IndirectLightingComponent() : technique(AmbientSettings()) {}

    explicit IndirectLightingComponent(AmbientSettings ambient) : technique(ambient) {}

    explicit IndirectLightingComponent(DDGISettings ddgi) : technique(ddgi) {}

    // Helper methods to check current technique
    bool isAmbient() const { return std::holds_alternative<AmbientSettings>(technique); }

    bool isDDGI() const { return std::holds_alternative<DDGISettings>(technique); }

    bool isDisabled() const { return std::holds_alternative<std::monostate>(technique) || !enabled; }

    // Helper getters (returns nullptr if wrong type)
    AmbientSettings *getAmbientSettings() { return std::get_if<AmbientSettings>(&technique); }

    const AmbientSettings *getAmbientSettings() const { return std::get_if<AmbientSettings>(&technique); }

    DDGISettings *getDDGISettings() { return std::get_if<DDGISettings>(&technique); }

    const DDGISettings *getDDGISettings() const { return std::get_if<DDGISettings>(&technique); }
};

} // namespace Rapture

#endif
