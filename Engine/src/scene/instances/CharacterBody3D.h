#ifndef RAPTURE__CHARACTER_BODY3D_H
#define RAPTURE__CHARACTER_BODY3D_H

#include "physics/CharacterBody.h"
#include "scene/instances/PhysicsBody3D.h"

#include <glm/glm.hpp>
#include <memory>

namespace Rapture {

class Node3D;

/**
 * @brief A body that walks the object it is part of through the world under its own control.
 */
class CharacterBody3D : public PhysicsBody3D {
  public:
    CharacterBody3D(Scene &scene, std::string_view name);
    ~CharacterBody3D() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Hands the object's rotation to the body before the step that sweeps with it
     * @param dt Seconds since the last update
     */
    void onUpdate(float dt) override;

    void applySimulatedTransform(const glm::vec3 &position, const glm::quat &rotation) override;

    /**
     * @brief Puts this body and the object it walks somewhere else outright
     * @param position World space position
     * @param rotation World space orientation
     */
    void teleport(const glm::vec3 &position, const glm::quat &rotation);

    /**
     * @brief Recreates the body from the current settings, at the transform its object sits at now
     */
    void rebuild();

    void setVelocity(const glm::vec3 &velocity) override;
    const glm::vec3 &velocity() const { return m_velocity; }

    /**
     * @brief Asks this body to jump, which lapses if no ground arrives in time
     */
    void jump();

    physics::GroundState groundState() const;
    bool isOnGround() const;

    const physics::CollisionShape &shape() const { return m_shape; }
    void setShape(const physics::CollisionShape &shape);

    /**
     * @brief The shape's offset from the object's origin
     */
    const glm::vec3 &shapeOffset() const { return m_shapeOffset; }
    void setShapeOffset(const glm::vec3 &offset);

    float mass() const { return m_mass; }
    void setMass(float mass);

    /**
     * @brief The steepest slope this body walks up, in radians
     */
    float maxSlopeAngle() const { return m_maxSlopeAngle; }
    void setMaxSlopeAngle(float radians);

    float stepUp() const { return m_stepUp; }
    void setStepUp(float stepUp);

    float stepDown() const { return m_stepDown; }
    void setStepDown(float stepDown);

    float jumpSpeed() const { return m_jumpSpeed; }
    void setJumpSpeed(float jumpSpeed);

    /**
     * @brief The object this body walks
     * @return The object, or nullptr if it is detached or has no place in the world
     */
    Node3D *node() const;

    physics::CharacterBody *body() const { return m_body.get(); }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    void onAttach() override;
    void onDetach() override;

  private:
    /**
     * @brief Hands the current velocity and any pending jump to the body
     * @param jump Whether a jump is being asked for
     */
    void pushMovement(bool jump);

    void releaseBody();

  private:
    physics::CollisionShape m_shape = physics::CapsuleShape{};
    glm::vec3 m_shapeOffset{0.0f};
    glm::vec3 m_velocity{0.0f};
    float m_mass = 70.0f;
    float m_maxSlopeAngle = glm::radians(50.0f);
    float m_stepUp = 0.4f;
    float m_stepDown = 0.5f;
    float m_jumpSpeed = 4.0f;

    std::unique_ptr<physics::CharacterBody> m_body;
};

} // namespace Rapture

#endif // RAPTURE__CHARACTER_BODY3D_H
