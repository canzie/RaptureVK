#ifndef RAPTURE__ANIMATION_COMPONENT_H
#define RAPTURE__ANIMATION_COMPONENT_H

#include <glm/glm.hpp>

namespace Rapture {

enum class FogType {
    Linear,
    Exponential,
    ExponentialSquared
};

struct AnimationComponent {

    AnimationComponent() = default;
};

} // namespace Rapture

#endif // RAPTURE__ANIMATION_COMPONENT_H
