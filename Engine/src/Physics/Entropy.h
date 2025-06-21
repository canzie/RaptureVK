#pragma once


/*
*   Entropy will be the physics engine built and used for the Rapture engine
*   The first version will be a impulse based approach
*   I will attempt to create the engine in such a way that there is room for force based solvers for specific cases
*
*/

#include "Scenes/Scene.h"
#include "Scenes/Entities/Entity.h"

#include "Colliders/ColliderPrimitives.h"


#include <glm/glm.hpp>

#include <cstdint>
#include <array>
#include <vector>
#include <memory>

namespace Rapture::Entropy {



    struct ContactPoint {
        glm::vec3 worldPointA;
        glm::vec3 worldPointB;
        glm::vec3 normalOnB;
        float penetrationDepth;
        float restitution;
        float friction;
    };

    struct ContactManifold {
        Entity entityA;
        Entity entityB;
        std::vector<ContactPoint> contactPoints;
    };

    // main class for the physics engine
    class EntropyPhysics {


    };

    // Should deal with collisions between colliders
    class EntropyCollisions {
    public:
        std::vector<ContactManifold> detectCollisions(std::shared_ptr<Scene> scene, float dt);

    private:
        void broadPhase(std::shared_ptr<Scene> scene);

        void narrowPhase(std::shared_ptr<Scene> scene, std::vector<ContactManifold> &manifolds);

        // general collision detection function
        bool checkCollision(const Entity& entityA, const Entity& entityB);

        // shape specific collision detection functions

        bool checkSphereSphere(SphereColliderComponent& sphereA, SphereColliderComponent& sphereB);
        // checks collision between 2 AABBs
        bool checkBoxBox(AABBColliderComponent& boxA, AABBColliderComponent& boxB);


    private:

        std::vector<std::pair<Entity, Entity>> m_potentialPairs;

    };


    // Should deal with dynamics like gravity
    // contains the physics solver
    class EntropyDynamics {


    };





} // namespace Rapture