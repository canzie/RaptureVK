#ifndef RAPTURE__AI_CONTROLLER_H
#define RAPTURE__AI_CONTROLLER_H

#include "scenes/entities/Entity.h"

namespace Rapture {

/**
 * @brief Early stub that drives a pawn from AI logic instead of device input
 *
 * Same possession model as PlayerController, but produces its own movement intent with
 * no Input source. Header-only placeholder, behavior logic to be added when AI is built.
 */
class AIController {
  public:
    AIController() = default;

    /**
     * @brief Possess a pawn for this controller to drive
     * @param pawn Pawn entity, needs a Transform
     */
    void possess(Entity pawn) { m_pawn = pawn; }

    /**
     * @brief Release the currently possessed pawn
     */
    void unpossess() { m_pawn = Entity(); }

    /**
     * @brief Get the currently possessed pawn
     * @return The possessed pawn, or an invalid Entity if none
     */
    Entity getPossessed() const { return m_pawn; }

    /**
     * @brief Advance the possessed pawn from AI logic
     * @param dt Delta time in seconds
     */
    void update(float dt);

  private:
    Entity m_pawn;
};

} // namespace Rapture

#endif // RAPTURE__AI_CONTROLLER_H
