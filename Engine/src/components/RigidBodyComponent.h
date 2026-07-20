#ifndef RAPTURE__RIGID_BODY_COMPONENT_H
#define RAPTURE__RIGID_BODY_COMPONENT_H

#include "physics/Common.h"

namespace Rapture {

struct RigidBodyComponent {
    PhysicsShape shape = PhysicsBoxShape{};
    PhysicsMotionType motionType = PHYSICS_MOTION_DYNAMIC;
    float friction = 0.2f;
    float restitution = 0.0f;
    bool startActive = true;
    bool shapeFromBounds = true;

    PhysicsBodyId bodyId;
};

} // namespace Rapture

#endif // RAPTURE__RIGID_BODY_COMPONENT_H
