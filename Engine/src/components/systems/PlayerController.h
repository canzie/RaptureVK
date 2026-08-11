#ifndef RAPTURE__PLAYER_CONTROLLER_H
#define RAPTURE__PLAYER_CONTROLLER_H

#include "input/ControlInput.h"
#include "ecs/entity_accessor.h"

namespace Rapture {

/**
 * @brief Early stub that drives a player-controlled pawn from ControlInput
 *
 * Possesses a pawn (the body it moves) and drives an associated camera: movement from
 * ControlInput.move (locomotion), look from ControlInput.look (routed to the camera rig).
 * Placeholder to be fleshed out when game-runtime play is built, adapt the shape as needed.
 */
class PlayerController {
  public:
    PlayerController() = default;

    /**
     * @brief Possess a pawn for this controller to drive
     * @param pawn Pawn entity, needs a Transform
     */
    void possess(ecs::EntityAccessor pawn);

    /**
     * @brief Release the currently possessed pawn
     */
    void unpossess();

    /**
     * @brief Get the currently possessed pawn
     * @return The possessed pawn, or an invalid Entity if none
     */
    ecs::EntityAccessor getPossessed() const { return m_pawn; }

    /**
     * @brief Set the camera this controller's look input drives
     * @param camera Camera entity
     */
    void setCamera(ecs::EntityAccessor camera) { m_camera = camera; }

    /**
     * @brief Advance the possessed pawn from this frame's intent
     * @param dt Delta time in seconds
     * @param input Device-agnostic input for this frame
     */
    void update(float dt, const ControlInput &input);

    float moveSpeed = 6.0f;
    float lookSensitivity = 0.2f;

  private:
    ecs::EntityAccessor m_pawn;
    ecs::EntityAccessor m_camera;
};

} // namespace Rapture

#endif // RAPTURE__PLAYER_CONTROLLER_H
