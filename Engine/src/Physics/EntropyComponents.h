#pragma once

#include "Colliders/ColliderPrimitives.h"
#include "precision.h"
#include "glm/gtc/quaternion.hpp"
#include "Components/Systems/Transforms.h"

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
    glm::quat       orientation       = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // omega?
    //bool lock xyx = true -> add constraints later



    // The transform of the body at the end of the last physics update
    Transforms previousTransform;
    bool       isFirstUpdate = true;

    RigidBodyComponent(std::unique_ptr<Entropy::ColliderBase> collider) 
        : collider(std::move(collider)) {}

    void setMass(Precision::real mass) {
        if (mass == 0.0f) {
            invMass = 0.0f;
            invInertiaTensor = glm::mat3(1.0f);
            return;
        }

        invMass = 1.0f / mass;
        setInertiaTensor(collider->calculateInertiaTensor(mass));
    }

    void setInertiaTensor(const glm::mat3& inertiaTensor) {
        invInertiaTensor = glm::inverse(inertiaTensor);
    }


};

}
