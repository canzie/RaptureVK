#include "Entropy.h"

#include "Components/Components.h"
#include "EntropyComponents.h"
#include "Meshes/MeshPrimitives.h"
#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "WindowContext/Application.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <set>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Rapture::Entropy {


std::vector<std::pair<Entity, Entity>>& EntropyCollisions::broadPhase(std::shared_ptr<Scene> scene)
{
    m_potentialPairs.clear();
    if (!scene) return m_potentialPairs;


    if (!m_staticBVH) {
        m_staticBVH = std::make_shared<BVH>(Rapture::LeafType::AABB);
        m_staticBVH->build(scene);
    }
    updateDynamicBVH(scene);

    auto nodes = m_dynamicBVH->getNodes();

    auto makeKey = [](uint32_t a, uint32_t b) {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    };
    std::unordered_set<uint64_t> pairSet;

    for (const auto &node : nodes) {
        if (!node.isLeaf())
            continue; // skip internal or free nodes

        auto entityHandle = node.entityID;
        Entity entity(entityHandle, scene.get());
        auto boundingBox = BoundingBox(node.min, node.max);


        // check against static BVH
        auto result = m_staticBVH->getIntersectingAABBs(boundingBox);

        for (auto otherEntityHandle : result) {
            if (otherEntityHandle == entityHandle) continue;
            uint64_t key = makeKey(entityHandle, otherEntityHandle);
            if (pairSet.insert(key).second) {
                Entity otherEntity(otherEntityHandle, scene.get());
                m_potentialPairs.push_back({entity, otherEntity});
            }
        }

        // check against dynamic BVH
        auto dynamicResult = m_dynamicBVH->getIntersectingAABBs(boundingBox);

        for (auto otherEntityHandle : dynamicResult) {
            if (otherEntityHandle == entityHandle) continue;
            uint64_t key = makeKey(entityHandle, otherEntityHandle);
            if (pairSet.insert(key).second) {
                Entity otherEntity(otherEntityHandle, scene.get());
                m_potentialPairs.push_back({entity, otherEntity});
            }
        }

    }


    return m_potentialPairs;
}

void EntropyCollisions::narrowPhase(std::shared_ptr<Scene> scene, std::vector<ContactManifold> &manifolds) {

    auto& reg = scene->getRegistry();
    auto view = reg.view<RigidBodyComponent, TransformComponent>();

    uint32_t contactPoints = 0;

    for (auto [entityA, entityB] : m_potentialPairs) {
        auto [rigidBodyA, transformA] = view.get<RigidBodyComponent, TransformComponent>(entityA);
        auto [rigidBodyB, transformB] = view.get<RigidBodyComponent, TransformComponent>(entityB);

        rigidBodyA.collider->transform = rigidBodyA.collider->localTransform * transformA.transformMatrix();
        rigidBodyB.collider->transform = rigidBodyB.collider->localTransform * transformB.transformMatrix();

        ContactManifold manifold;
        if (rigidBodyA.collider->intersects(*rigidBodyB.collider, &manifold)) {
            manifold.entityA = entityA;
            manifold.entityB = entityB;
            manifolds.push_back(manifold);
            contactPoints += manifold.contactPoints.size();

            // Apply default material properties for now
            for (auto &cp : manifold.contactPoints) {
                cp.restitution = 0.0f; // inelastic
                cp.friction    = 0.6f; // placeholder
            }
        }
    }



    //RP_CORE_INFO("Found {0} manifolds, {1} contact points", manifolds.size(), contactPoints);

}

bool EntropyCollisions::checkCollision(const Entity &entityA, const Entity &entityB)
{
    return false;
}

void EntropyCollisions::updateVisualization(const std::vector<Rapture::BVHNode> &nodes, std::shared_ptr<Entity> vizEntity, const glm::vec4 &color) {

   if (vizEntity && vizEntity->isValid() && vizEntity->hasComponent<Rapture::InstanceShapeComponent>()) {
        auto& instanceComp = vizEntity->getComponent<Rapture::InstanceShapeComponent>();
        instanceComp.color = color;

        // Re-build instanceData from current DBVH leaves.
        std::vector<Rapture::InstanceData> instanceData;
        for (const auto& node : nodes) {
            if (node.isLeaf()) {
                glm::vec3 center = (node.min + node.max) * 0.5f;
                glm::vec3 size   = node.max - node.min;

                glm::mat4 transform = glm::translate(glm::mat4(1.0f), center);
                transform = glm::scale(transform, size);
                instanceData.push_back({transform});
            }
        }


        // Check if buffer needs to grow/shrink.
        if (instanceData.size() != instanceComp.instanceCount) {
            // Recreate SSBO with new size.
            instanceComp.instanceSSBO = std::make_shared<Rapture::StorageBuffer>(
                sizeof(Rapture::InstanceData) * instanceData.size(),
                Rapture::BufferUsage::DYNAMIC,
                Rapture::Application::getInstance().getVulkanContext().getVmaAllocator());

            instanceComp.instanceCount = static_cast<uint32_t>(instanceData.size());
        }

        // Upload data (replace entire contents).
        if (!instanceData.empty()) {
            instanceComp.instanceSSBO->addData(instanceData.data(), instanceData.size() * sizeof(Rapture::InstanceData), 0);
        }
    } 


}




std::vector<ContactManifold> EntropyCollisions::detectCollisions(std::shared_ptr<Scene> scene, float dt)
{
    // For now just run broad phase and store the potential pairs.
    broadPhase(scene);

    // 2. Narrow phase – generate manifolds
    std::vector<ContactManifold> manifolds;
    narrowPhase(scene, manifolds);

    return manifolds;
}

void EntropyCollisions::updateDynamicBVH(std::shared_ptr<Scene> scene) {

    if (!m_dynamicBVH) {
        m_dynamicBVH = std::make_shared<DBVH>();
        m_entityNodeMap.clear();
    } 

    auto& reg = scene->getRegistry();
    auto view = reg.view<RigidBodyComponent, MeshComponent, TransformComponent, BoundingBoxComponent>();

    // We need direct access to the node list to look up node indices by entity ID.
    auto& nodes = m_dynamicBVH->getNodes();

    for (auto entityHandle : view) // TODO : NEEDS TO BE GIGA OPTIMISED
    {
        auto [rigidBody, mesh, transform, boundingBox] = view.get<RigidBodyComponent, MeshComponent, TransformComponent, BoundingBoxComponent>(entityHandle);


        if (mesh.isStatic) {
            continue;
        }


        glm::vec3 minLocal, maxLocal;
        rigidBody.collider->getAABB(minLocal, maxLocal);

        BoundingBox aabb = BoundingBox(minLocal, maxLocal);

        BoundingBox localAABB = aabb.transform(rigidBody.collider->localTransform) + boundingBox.localBoundingBox;


        BoundingBox worldAABB = localAABB.transform(transform.transformMatrix());

        // Locate the leaf node in BVH corresponding to this entity using map.
        int nodeId = -1;
        auto it = m_entityNodeMap.find((uint32_t)entityHandle);
        if (it != m_entityNodeMap.end()) nodeId = it->second;

        if (nodeId != -1) {
            m_dynamicBVH->update(nodeId, worldAABB);
        }
        else {
            // Entity was added after the BVH build – insert a new leaf.
            int newId = m_dynamicBVH->insert(static_cast<uint32_t>(entityHandle), worldAABB);
            m_entityNodeMap[(uint32_t)entityHandle] = newId;
        }
    }
}


void EntropyCollisions::debugVisualize(std::shared_ptr<Scene> scene) {

    auto& app = Rapture::Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();

    if (!m_staticVizEntity) {
        auto cube = std::make_shared<Rapture::Mesh>(Rapture::Primitives::CreateCube());

        m_staticVizEntity = std::make_shared<Entity>(scene->createEntity("Static BVH Visualization"));
        m_staticVizEntity->addComponent<TransformComponent>();
        m_staticVizEntity->addComponent<InstanceShapeComponent>(std::vector<InstanceData>(), vulkanContext.getVmaAllocator());
        m_staticVizEntity->addComponent<MeshComponent>(cube);


        updateVisualization(m_staticBVH->getNodes(), m_staticVizEntity, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        

    }

    if (!m_dynamicVizEntity) {
        auto cube = std::make_shared<Rapture::Mesh>(Rapture::Primitives::CreateCube());

        m_dynamicVizEntity = std::make_shared<Entity>(scene->createEntity("Dynamic BVH Visualization"));
        m_dynamicVizEntity->addComponent<TransformComponent>();
        m_dynamicVizEntity->addComponent<InstanceShapeComponent>(std::vector<InstanceData>(), vulkanContext.getVmaAllocator());
        m_dynamicVizEntity->addComponent<MeshComponent>(cube);
    }



    if (m_dynamicVizEntity) {
        updateVisualization(m_dynamicBVH->getNodes(), m_dynamicVizEntity, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    }

}

// -------------------------------------------------------------
//  EntropyDynamics implementation
// -------------------------------------------------------------

void EntropyDynamics::addGlobalForceGenerator(const std::shared_ptr<ForceGenerator> &gen) {
    if (gen) m_globalGenerators.push_back(gen);
}

void EntropyDynamics::addBodyForceGenerator(Entity entity, const std::shared_ptr<ForceGenerator> &gen) {
    if (!gen || !entity.isValid()) return;
    m_bodyGenerators[entity.getID()].push_back(gen);
}

void EntropyDynamics::step(std::shared_ptr<Scene> scene, float dt) {
    if (!scene) return;

    auto &reg = scene->getRegistry();
    auto view = reg.view<RigidBodyComponent, TransformComponent>();

    // 1) Apply global forces to every body.
    for (auto entityHandle : view) {
        auto &rb = view.get<RigidBodyComponent>(entityHandle);
        for (auto &gen : m_globalGenerators) {
            gen->apply(rb, dt);
        }
    }

    // 2) Apply per-body forces.
    for (auto &[id, gens] : m_bodyGenerators) {
        Entity e = Entity(id, scene.get());
        if (!e.isValid() || !view.contains(e)) continue;
        auto &rb = view.get<RigidBodyComponent>(e);
        for (auto &gen : gens) {
            gen->apply(rb, dt);
        }
    }

    // 3) Integrate velocities from accumulated forces/torques.
    integrate(scene, dt);
}

void EntropyDynamics::integrate(std::shared_ptr<Scene> scene, float dt) {

    auto& reg = scene->getRegistry();
    auto view = reg.view<RigidBodyComponent, TransformComponent>();

    for (auto entityHandle : view) {
        auto [rb, transform] = view.get<RigidBodyComponent, TransformComponent>(entityHandle);
        if (rb.invMass == 0.0f) {
            rb.velocity = glm::vec3(0.0f);
            rb.angularVelocity = glm::vec3(0.0f);
            continue; // Static body – no integration
        }

        // Linear: v += (F / m) * dt   (F already accumulated)
        glm::vec3 acceleration = rb.accumulatedForce * rb.invMass;
        rb.velocity += acceleration * dt;

        // Angular: ω += I^-1 * τ * dt
        glm::vec3 angularAcc = rb.invInertiaTensor * rb.accumulatedTorque;
        rb.angularVelocity += angularAcc * dt;

        // Integrate position (Euler)
        glm::vec3 newPos = transform.transforms.getTranslation() + rb.velocity * dt;
        transform.transforms.setTranslation(newPos);

        // Integrate orientation using angular velocity (Euler, small-angle)
        glm::quat currentQ = transform.transforms.getRotationQuat();
        glm::quat omegaQuat(0.0f, rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z);
        glm::quat deltaQ = 0.5f * omegaQuat * currentQ;
        glm::quat newQ = glm::normalize(currentQ + deltaQ * dt);
        transform.transforms.setRotation(newQ);

        rb.accumulatedForce  = glm::vec3(0.0f);
        rb.accumulatedTorque = glm::vec3(0.0f);
    }


}

// -------------------------------------------------------------
//  EntropyPhysics high-level step
// -------------------------------------------------------------

void EntropyPhysics::addGlobalForceGenerator(const std::shared_ptr<ForceGenerator> &gen) {
    m_dynamics.addGlobalForceGenerator(gen);
}

void EntropyPhysics::addBodyForceGenerator(Entity entity, const std::shared_ptr<ForceGenerator> &gen) {
    m_dynamics.addBodyForceGenerator(entity, gen);
}

std::vector<ContactManifold> EntropyPhysics::step(std::shared_ptr<Scene> scene, float dt)
{
    // 1. Apply forces & integrate velocities
    m_dynamics.step(scene, dt);

    // 2. Run collision detection
    auto manifolds = m_collisions.detectCollisions(scene, dt);

    // 3. Solve collision constraints (sequential impulse)
    if (!m_solver) m_solver = std::make_unique<ConstraintSolver>();
    m_solver->solve(scene, manifolds, dt);

    return manifolds;
}

// -------------------------------------------------------------
//  ConstraintSolver implementation
// -------------------------------------------------------------
using Constraint = ConstraintSolver::ContactConstraint;

void ConstraintSolver::buildConstraints(std::shared_ptr<Scene> scene,
                                        const std::vector<ContactManifold>& manifolds) {
    m_constraints.clear();
    if (!scene) return;

    auto& reg = scene->getRegistry();

    for (const auto &manifold : manifolds) {
        if (manifold.contactPoints.empty()) continue;

        // Grab components once per manifold
        auto &rbA = reg.get<RigidBodyComponent>(manifold.entityA);
        auto &rbB = reg.get<RigidBodyComponent>(manifold.entityB);
        auto &trA = reg.get<TransformComponent>(manifold.entityA);
        auto &trB = reg.get<TransformComponent>(manifold.entityB);

        for (const auto &cp : manifold.contactPoints) {
            Constraint c;
            c.a = manifold.entityA;
            c.b = manifold.entityB;
            c.normal = glm::normalize(cp.normalOnB); // ensure normal is normalised
            c.restitution = cp.restitution;
            c.friction    = cp.friction;
            c.penetration = cp.penetrationDepth;
            c.contactPoint = 0.5f * (cp.worldPointA + cp.worldPointB);

            c.bodyA  = &rbA;
            c.bodyB  = &rbB;
            c.transA = &trA;
            c.transB = &trB;

            // Compute the relative contact vectors (from COM to contact point)
            c.ra = c.contactPoint - trA.transforms.getTranslation();
            c.rb = c.contactPoint - trB.transforms.getTranslation();

            m_constraints.push_back(c);
        }
    }
}

void ConstraintSolver::resolveInterpenetration(uint32_t iterations) {
    const float penetrationEpsilon = 0.0001f;
    for (uint32_t it = 0; it < iterations; ++it) {
        // Select the deepest penetration
        float maxPenetration = penetrationEpsilon;
        int constraintIndex = -1;

        // based on the penetration depth, looks most realistic by handling the most extreme cases first
        for (size_t i = 0; i < m_constraints.size(); ++i) {
            if (m_constraints[i].penetration > maxPenetration) {
                maxPenetration = m_constraints[i].penetration;
                constraintIndex = static_cast<int>(i);
            }
        }

        if (constraintIndex == -1) break; // all resolved

        Constraint &c = m_constraints[constraintIndex];

        // Compute total inverse mass. Static bodies have invMass = 0.
        float totalInvMass = c.bodyA->invMass + c.bodyB->invMass;
        if (totalInvMass <= 0.0f) {
            c.penetration = 0.0f; // cannot resolve
            continue;
        }

        // Calculate the positional correction magnitude per inverse mass.
        glm::vec3 movePerInvMass = c.normal * (c.penetration / totalInvMass);

        // Apply the positional changes.
        // will result in 0 for static bodies since invMass is 0
        glm::vec3 newPosA = c.transA->transforms.getTranslation() + movePerInvMass * c.bodyA->invMass;
        c.transA->transforms.setTranslation(newPosA);
    
        glm::vec3 newPosB = c.transB->transforms.getTranslation() - movePerInvMass * c.bodyB->invMass;
        c.transB->transforms.setTranslation(newPosB);
    

        // Mark this constraint as resolved for penetration.
        c.penetration = 0.0f;
    }
}

void ConstraintSolver::resolveVelocities(float dt, uint32_t iterations) {
    const float velocityEpsilon = 0.0001f;
    if (dt <= 0.0f) return;

    for (uint32_t it = 0; it < iterations; ++it) {
        bool allResolved = true;

        for (auto &c : m_constraints) {
            // Relative velocity at contact along the normal
            glm::vec3 relVel = c.bodyA->velocity - c.bodyB->velocity;
            float separatingVel = glm::dot(relVel, c.normal);

            // Calculate desired separating velocity after collision
            float desiredSepVel = -separatingVel * c.restitution;

            // Check if need to resolve
            float deltaVel = desiredSepVel - separatingVel;
            if (deltaVel < velocityEpsilon) {
                continue; // either moving apart already or small
            }

            allResolved = false;

            float totalInvMass = c.bodyA->invMass + c.bodyB->invMass;
            if (totalInvMass <= 0.0f) continue;

            float impulseMag = deltaVel / totalInvMass;
            glm::vec3 impulse = impulseMag * c.normal;

            // will result in 0 for static bodies since invMass is 0
            c.bodyA->velocity += impulse * c.bodyA->invMass;
            c.bodyB->velocity -= impulse * c.bodyB->invMass;
            
        }

        if (allResolved) break; // early out when all constraints satisfied
    }
}

void ConstraintSolver::solve(std::shared_ptr<Scene> scene,
                             const std::vector<ContactManifold>& manifolds,
                             float dt,
                             uint32_t iterations) {
    if (!scene || manifolds.empty()) return;

    // 1. Build constraints from the current contacts
    buildConstraints(scene, manifolds);

    // 2. Positional correction (interpenetration)
    resolveInterpenetration(iterations);

    // 3. Velocity resolution (impulses)
    resolveVelocities(dt, iterations);
}

} // namespace Rapture::Entropy
