#pragma once


/*
*   Entropy will be the physics engine built and used for the Rapture engine
*   The first version will be a impulse based approach
*   I will attempt to create the engine in such a way that there is room for force based solvers for specific cases
*
*/

#include "Scenes/Scene.h"

#include "Colliders/ColliderPrimitives.h"
#include "AccelerationStructures/CPU/BVH/BVH.h"
#include "AccelerationStructures/CPU/BVH/DBVH.h"

#include "EntropyCommon.h"
#include "EntropyComponents.h"

#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Rapture { struct TransformComponent; }

namespace Rapture::Entropy {


    class ConstraintSolver;
    class ForceGenerator;
    class EntropyDynamics;
    class EntropyCollisions;



    // Should deal with collisions between colliders
    class EntropyCollisions {
    public:
        std::vector<ContactManifold> detectCollisions(std::shared_ptr<Scene> scene, float dt);


        // Visualise current BVHs (static + dynamic) using InstanceShapeComponents
        void debugVisualize(std::shared_ptr<Scene> scene);

        std::vector<std::pair<Entity, Entity>>& broadPhase(std::shared_ptr<Scene> scene);

    private:

        void narrowPhase(std::shared_ptr<Scene> scene, std::vector<ContactManifold> &manifolds);

        // general collision detection function
        bool checkCollision(const Entity& entityA, const Entity& entityB);

        // shape specific collision detection functions

        //bool checkSphereSphere(SphereCollider& sphereA, SphereCollider& sphereB);
        // checks collision between 2 AABBs
        //bool checkBoxBox(AABBCollider& boxA, AABBCollider& boxB);

        void updateVisualization(const std::vector<Rapture::BVHNode>& nodes,
                                 std::shared_ptr<Entity> vizEntity,
                                 const glm::vec4& color);

        void updateDynamicBVH(std::shared_ptr<Scene> scene);


    private:

        std::vector<std::pair<Entity, Entity>> m_potentialPairs;

        // Acceleration structures
        std::shared_ptr<BVH>  m_staticBVH;
        std::shared_ptr<DBVH> m_dynamicBVH;

        // Debug visualisation entities
        std::shared_ptr<Entity> m_staticVizEntity;
        std::shared_ptr<Entity> m_dynamicVizEntity;

        std::unordered_map<uint32_t,int> m_entityNodeMap;

    };
    


    // -------------------------------------------------------------
    //  Force generation interface and common generators
    // -------------------------------------------------------------
    
    class ForceGenerator {
    public:
        virtual ~ForceGenerator() = default;
        // Apply a force (and possibly torque) to the given rigid body.
        virtual void apply(RigidBodyComponent &rb, float dt) = 0;
    };

    // Simple uniform gravity force (F = m * g)
    class GravityForce final : public ForceGenerator {
    public:
        explicit GravityForce(const glm::vec3 &g = glm::vec3(0.0f, -9.81f, 0.0f)) : m_gravity(g) {}
        void setGravity(const glm::vec3 &g) { m_gravity = g; }
        const glm::vec3 &getGravity() const { return m_gravity; }
        void apply(RigidBodyComponent &rb, float dt) override {
            // Skip static/infinite-mass bodies.
            if (rb.invMass == 0.0f) return;
            rb.accumulatedForce += m_gravity * (1.0f / rb.invMass); // mass = 1 / invMass
        }
    private:
        glm::vec3 m_gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    };

    // Damping force
    class DampingForce final : public ForceGenerator {
    public:
        DampingForce(float lin = 0.2f, float ang = 0.2f) : m_linear(lin), m_angular(ang) {}
        void setCoefficients(float lin, float ang) { m_linear = lin; m_angular = ang; }
        void apply(RigidBodyComponent &rb, float /*dt*/) override {
            if (rb.invMass == 0.0f) return;
            rb.accumulatedForce  += -m_linear  * rb.velocity;
            rb.accumulatedTorque += -m_angular * rb.angularVelocity;
        }
    private:
        float m_linear;
        float m_angular;
    };



    // Should deal with dynamics like gravity
    // contains the physics solver
    class EntropyDynamics {
    public:
        // Register global generator (applied to every dynamic body each step).
        void addGlobalForceGenerator(const std::shared_ptr<ForceGenerator> &gen);
        // Attach a generator to a specific entity (e.g., custom thruster).
        void addBodyForceGenerator(Entity entity, const std::shared_ptr<ForceGenerator> &gen);

        // Executes force application and velocity integration for all rigid bodies in the scene.
        void step(std::shared_ptr<Scene> scene, float dt);

    private:
        void integrate(std::shared_ptr<Scene> scene, float dt);

    private:
        std::vector<std::shared_ptr<ForceGenerator>> m_globalGenerators;
        std::unordered_map<uint32_t, std::vector<std::shared_ptr<ForceGenerator>>> m_bodyGenerators; // key: Entity id
    };



    // main class for the physics engine
    class EntropyPhysics {
    public:
        EntropyPhysics() = default;

        // Forward force-generator management to dynamics subsystem
        void addGlobalForceGenerator(const std::shared_ptr<ForceGenerator> &gen);

        void addBodyForceGenerator(Entity entity, const std::shared_ptr<ForceGenerator> &gen);

        // Run full physics pipeline
        std::vector<ContactManifold> step(std::shared_ptr<Scene> scene, float dt);

        EntropyCollisions* getCollisions() { return &m_collisions; }
        EntropyDynamics* getDynamics() { return &m_dynamics; }

    private:
        EntropyDynamics   m_dynamics;
        EntropyCollisions m_collisions;
        
        std::unique_ptr<ConstraintSolver> m_solver;
    };


    class ConstraintSolver {
    public:
        ConstraintSolver() = default;

        void solve(std::shared_ptr<Scene> scene,
                    const std::vector<ContactManifold>& manifolds,
                    float dt,
                    uint32_t iterations = 8);

    struct ContactConstraint {
        Entity a;
        Entity b;
        glm::vec3 normal;
        float restitution;
        float friction;
        float penetration;
        glm::vec3 ra; // contact vector from A's COM to contact point
        glm::vec3 rb; // contact vector from B's COM to contact point
        glm::vec3 contactPoint; // world-space contact location (average of points)

        // Cached pointers for quick access during iterations
        RigidBodyComponent* bodyA  = nullptr;
        RigidBodyComponent* bodyB  = nullptr;
        TransformComponent* transA = nullptr;
        TransformComponent* transB = nullptr;
    };

    private:
        // Build internal constraints from manifolds for the current frame
        void buildConstraints(std::shared_ptr<Scene> scene,
                              const std::vector<ContactManifold>& manifolds);

        // Positional correction (interpenetration) phase
        void resolveInterpenetration(uint32_t iterations);

        // Velocity resolution phase
        void resolveVelocities(float dt, uint32_t iterations);

        std::vector<ContactConstraint> m_constraints;
    };

} // namespace Rapture::Entropy