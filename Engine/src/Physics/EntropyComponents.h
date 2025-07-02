#pragma once

#include "Colliders/ColliderPrimitives.h"
#include "precision.h"
#include "glm/gtc/quaternion.hpp"

#include <memory>

namespace Rapture::Entropy {

struct RigidBodyComponent {
    std::unique_ptr<Entropy::ColliderBase> collider;
    Precision::real invMass           = 0.0f;
    glm::mat3       invInertiaTensor  = glm::mat3(1.0f);
    glm::vec3       velocity          = glm::vec3(0.0f);
    glm::vec3       angularVelocity   = glm::vec3(0.0f);
    glm::vec3       accumulatedForce  = glm::vec3(0.0f);
    glm::vec3       accumulatedTorque = glm::vec3(0.0f);
    glm::quat       orientation       = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  //bool lock xyx = true -> add constraints later

    RigidBodyComponent(std::unique_ptr<Entropy::ColliderBase> collider) 
        : collider(std::move(collider)) {}

    void setMass(Precision::real mass) {
        invMass = 1.0f / mass;
    }

    void setInertiaTensor(const glm::mat3& inertiaTensor) {
        invInertiaTensor = glm::inverse(inertiaTensor);
    }

    void setInertia(glm::vec3& inertia) {
        glm::mat3 inertiaTensor = glm::mat3(inertia.x, 0.0f, 0.0f, 0.0f, inertia.y, 0.0f, 0.0f, 0.0f, inertia.z);
        setInertiaTensor(inertiaTensor);
        glm::mat3 R = glm::mat3_cast(orientation);
        invInertiaTensor = R * invInertiaTensor * glm::transpose(R);
    }

};

}
