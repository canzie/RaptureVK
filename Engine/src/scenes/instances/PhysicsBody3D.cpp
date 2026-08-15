#include "PhysicsBody3D.h"

#include "logging/Log.h"
#include "physics/CharacterBody.h"
#include "physics/PhysicsSystem.h"
#include "physics/RigidBody.h"
#include "scenes/Scene.h"

namespace Rapture {

PhysicsBody3D::PhysicsBody3D(Scene &scene, std::string_view name) : SceneComponent(scene, name) {}

const TypeInfo &PhysicsBody3D::staticType()
{
    static const TypeInfo type("PhysicsBody3D", &SceneComponent::staticType());
    return type;
}

const TypeInfo &PhysicsBody3D::type() const
{
    return staticType();
}

std::unique_ptr<physics::RigidBody> PhysicsBody3D::createRigidBody(const physics::RigidBodyConfig &config)
{
    PhysicsSystem *physicsSystem = scene() != nullptr ? scene()->physicsSystem() : nullptr;
    if (physicsSystem == nullptr) {
        RP_CORE_ERROR("'{}' cannot join a scene that has no simulation", name());
        return nullptr;
    }

    return physicsSystem->createRigidBody(config, this);
}

std::unique_ptr<physics::CharacterBody> PhysicsBody3D::createCharacterBody(const physics::CharacterBodyConfig &config)
{
    PhysicsSystem *physicsSystem = scene() != nullptr ? scene()->physicsSystem() : nullptr;
    if (physicsSystem == nullptr) {
        RP_CORE_ERROR("'{}' cannot join a scene that has no simulation", name());
        return nullptr;
    }

    return physicsSystem->createCharacterBody(config, this);
}

} // namespace Rapture
