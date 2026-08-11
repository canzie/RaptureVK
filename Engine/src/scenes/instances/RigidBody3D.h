#ifndef RAPTURE__RIGID_BODY3D_H
#define RAPTURE__RIGID_BODY3D_H

#include "physics/Common.h"
#include "scenes/instances/Node3D.h"

namespace Rapture {

/**
 * @brief A body in the rigid body simulation.
 *
 * Either stands in the scene tree on its own, or is held as a member by the node it moves, in
 * which case it is off tree and its own transform is the constant offset of the body from that
 * node. The simulation owns the transform of whichever of the two it drives.
 */
class RigidBody3D : public Node3D {
  public:
    /**
     * @brief Creates a body and adds it to the scene's simulation
     * @param scene The scene whose simulation the body joins
     * @param name Name for the body
     * @param host The node the body moves, or nullptr if the body moves itself
     */
    RigidBody3D(Scene &scene, std::string_view name, Node3D *host = nullptr);
    ~RigidBody3D() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Hands a simulated transform to the node this body drives
     * @param position Where the simulation put the body
     * @param rotation How the simulation oriented the body
     */
    void applySimulatedTransform(const glm::vec3 &position, const glm::quat &rotation);

    /**
     * @brief Recreates the body from the current settings, at the transform its node sits at now
     */
    void rebuild();

    const PhysicsShape &shape() const { return m_shape; }
    void setShape(const PhysicsShape &shape);

    PhysicsMotionType motionType() const { return m_motionType; }
    void setMotionType(PhysicsMotionType motionType);

    float friction() const { return m_friction; }
    void setFriction(float friction);

    float restitution() const { return m_restitution; }
    void setRestitution(float restitution);

    /**
     * @brief The node this body moves, which is the body itself when it stands on its own
     */
    Node3D *host() const { return m_host; }

    PhysicsBodyId bodyId() const { return m_bodyId; }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  private:
    /**
     * @brief The node the simulated transform is written to
     */
    Node3D *target() const;

    /**
     * @brief The body's rigid offset from its node, in world units
     * @return The offset, identity when the body stands on its own
     */
    glm::mat4 offsetTransform() const;

    /**
     * @brief Where the body itself sits in the world
     * @param offset The offset from offsetTransform
     * @return The body's world transform, without scale
     */
    glm::mat4 bodyTransform(const glm::mat4 &offset) const;

  private:
    Node3D *m_host = nullptr;

    PhysicsShape m_shape = PhysicsBoxShape{};
    PhysicsMotionType m_motionType = PHYSICS_MOTION_DYNAMIC;
    float m_friction = 0.2f;
    float m_restitution = 0.0f;
    bool m_startActive = true;

    PhysicsBodyId m_bodyId;
    glm::mat4 m_inverseOffset{1.0f};
};

} // namespace Rapture

#endif // RAPTURE__RIGID_BODY3D_H
