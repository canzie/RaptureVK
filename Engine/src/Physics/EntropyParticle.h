#pragma once

#include "precision.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>




namespace Rapture::Entropy {


enum class ParticleType {
    Bullet
};

class EntropyParticle {
public:
    virtual bool update(Precision::real dt) = 0;
    virtual void integrate(Precision::real dt) = 0;



private:
        glm::vec3 position;
        glm::vec3 velocity;
        glm::vec3 acceleration;
        Precision::real damping;
        Precision::real mass;
        Precision::real inverseMass;

        float startTime;
        ParticleType type;

    };


} // namespace Rapture::Entropy