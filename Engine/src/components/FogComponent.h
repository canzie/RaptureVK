#ifndef RAPTURE__FOGCOMPONENT_H
#define RAPTURE__FOGCOMPONENT_H

#include <glm/glm.hpp>

namespace Rapture {

enum class FogType {
    Linear,
    Exponential,
    ExponentialSquared
};

struct FogComponent {
    glm::vec3 color = glm::vec3(0.5f, 0.5f, 0.5f);
    float density = 0.01f;
    float start = 10.0f;  // for linear fog
    float end = 100.0f;   // for linear fog
    FogType type = FogType::Exponential;
    bool enabled = true;

    FogComponent() = default;

    FogComponent(glm::vec3 color, float density, FogType type = FogType::Exponential)
        : color(color), density(density), type(type) {}
};

} // namespace Rapture

#endif
