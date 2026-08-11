#include "PlayerController.h"

namespace Rapture {

void PlayerController::possess(ecs::EntityAccessor pawn)
{
    m_pawn = pawn;
}

void PlayerController::unpossess()
{
    m_pawn = ecs::EntityAccessor();
}

void PlayerController::update(float dt, const ControlInput &input)
{
    if (!m_pawn.isValid()) {
        return;
    }

    // TODO: early stub. Apply input.move as camera-relative locomotion to the pawn,
    // route input.look to m_camera's rig (spring arm). Both depend on the camera rig
    // and movement systems that do not exist yet.
    (void)dt;
    (void)input;
}

} // namespace Rapture
