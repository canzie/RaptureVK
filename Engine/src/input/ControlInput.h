#ifndef RAPTURE__CONTROL_INPUT_H
#define RAPTURE__CONTROL_INPUT_H

#include <glm/glm.hpp>

namespace Rapture {

/**
 * @brief Device-agnostic, per-frame input for a camera or pawn controller.
 *
 * Produced by a mapping layer from a raw Input. Controllers consume this and
 * never learn which physical device or key produced it.
 */
struct ControlInput {
    glm::vec2 look = {0.0f, 0.0f};       ///< Look delta; x = right positive, y = down positive
    glm::vec3 move = {0.0f, 0.0f, 0.0f}; ///< Movement axes in [-1, 1]: x = strafe, y = up, z = forward
    float zoom = 0.0f;                   ///< Zoom / dolly amount this frame
    bool jump = false;                   ///< Leave the ground
    bool orbit = false;                  ///< Engage orbit-around-focus
    bool pan = false;                    ///< Engage screen-space pan
    bool releaseControl = false;         ///< Request handing control back to the editor
};

} // namespace Rapture

#endif // RAPTURE__CONTROL_INPUT_H
