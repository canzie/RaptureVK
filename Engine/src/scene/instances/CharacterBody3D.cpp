#include "CharacterBody3D.h"

#include "scene/systems/Transforms.h"
#include "core/utils/Log.h"
#include "physics/Serialization.h"
#include "scene/Scene.h"
#include "scene/instances/Node3D.h"

namespace Rapture {

static constexpr std::string_view KEY_BODY = "characterBody";
static constexpr std::string_view KEY_SHAPE_OFFSET = "shapeOffset";
static constexpr std::string_view KEY_MASS = "mass";
static constexpr std::string_view KEY_MAX_SLOPE_ANGLE = "maxSlopeAngle";
static constexpr std::string_view KEY_STEP_UP = "stepUp";
static constexpr std::string_view KEY_STEP_DOWN = "stepDown";
static constexpr std::string_view KEY_JUMP_SPEED = "jumpSpeed";

static void s_writeVec3(WriteNode node, std::string_view key, const glm::vec3 &value)
{
    WriteNode values = node.addArray(key);
    values.append(value.x);
    values.append(value.y);
    values.append(value.z);
}

static glm::vec3 s_readVec3(ReadNode node, const glm::vec3 &fallback)
{
    if (node.size() != 3) {
        return fallback;
    }

    return glm::vec3(static_cast<float>(node.at(0).asF64(fallback.x)), static_cast<float>(node.at(1).asF64(fallback.y)),
                     static_cast<float>(node.at(2).asF64(fallback.z)));
}

CharacterBody3D::CharacterBody3D(Scene &scene, std::string_view name) : PhysicsBody3D(scene, name)
{
    setTickPhase(TICK_PRE_PHYSICS);
    setTickEnabled(true);
}

CharacterBody3D::~CharacterBody3D()
{
    releaseBody();
}

const TypeInfo &CharacterBody3D::staticType()
{
    static const TypeInfo type("CharacterBody3D", &PhysicsBody3D::staticType());
    return type;
}

const TypeInfo &CharacterBody3D::type() const
{
    return staticType();
}

Node3D *CharacterBody3D::node() const
{
    SceneObject *object = owner();
    return object != nullptr ? object->as<Node3D>() : nullptr;
}

void CharacterBody3D::onAttach()
{
    if (node() == nullptr) {
        RP_CORE_ERROR("'{}' needs an object with a place in the world", name());
        return;
    }

    rebuild();
}

void CharacterBody3D::onDetach()
{
    releaseBody();
}

void CharacterBody3D::releaseBody()
{
    m_body.reset();
}

void CharacterBody3D::rebuild()
{
    Node3D *target = node();
    if (target == nullptr) {
        return;
    }

    // a rebuild is a new body standing where the old one did, so the motion it had is carried over
    // rather than restarting the fall from nothing
    const glm::vec3 carriedVelocity = m_body != nullptr ? m_body->linearVelocity() : glm::vec3(0.0f);

    releaseBody();

    const glm::mat4 world = target->worldTransform();

    const glm::vec3 scale = transform::scale(world);

    physics::CharacterBodyConfig config;
    config.shape = physics::CollisionShape_scaled(m_shape, scale);
    config.shapeOffset = m_shapeOffset * scale;
    config.position = transform::translation(world);
    config.rotation = transform::rotation(world);
    config.mass = m_mass;
    config.maxSlopeAngle = m_maxSlopeAngle;
    config.stepUp = m_stepUp;
    config.stepDown = m_stepDown;

    m_body = createCharacterBody(config);
    if (m_body == nullptr) {
        return;
    }

    m_body->setLinearVelocity(carriedVelocity);
    pushMovement(false);
}

void CharacterBody3D::onUpdate(float dt)
{
    (void)dt;

    Node3D *target = node();
    if (m_body == nullptr || target == nullptr) {
        return;
    }

    // the simulation never turns a character, so whatever drives the object owns its rotation and
    // the step ahead has to sweep with the one the object is wearing now
    m_body->setRotation(transform::rotation(target->worldTransform()));
}

void CharacterBody3D::teleport(const glm::vec3 &position, const glm::quat &rotation)
{
    Node3D *target = node();
    if (target == nullptr) {
        return;
    }

    target->setWorldTransform(transform::compose(position, rotation, target->scale()));

    if (m_body != nullptr) {
        m_body->setTransform(position, rotation);
    }
}

void CharacterBody3D::applySimulatedTransform(const glm::vec3 &position, const glm::quat &rotation)
{
    (void)rotation;

    Node3D *target = node();
    if (target == nullptr) {
        return;
    }

    // only the position is taken back, so whatever turns the object keeps owning its rotation
    const glm::mat4 world = target->worldTransform();
    target->setSimulatedWorldTransform(transform::compose(position, transform::rotation(world), transform::scale(world)));
}

void CharacterBody3D::pushMovement(bool jump)
{
    if (m_body == nullptr) {
        return;
    }

    physics::CharacterBodyMovement movement;
    movement.velocity = m_velocity;
    movement.jump = jump;
    movement.jumpSpeed = m_jumpSpeed;

    m_body->setMovement(movement);
}

void CharacterBody3D::setVelocity(const glm::vec3 &velocity)
{
    m_velocity = velocity;
    pushMovement(false);
}

void CharacterBody3D::jump()
{
    pushMovement(true);
}

physics::GroundState CharacterBody3D::groundState() const
{
    return m_body != nullptr ? m_body->groundState() : physics::GROUND_IN_AIR;
}

bool CharacterBody3D::isOnGround() const
{
    return groundState() == physics::GROUND_ON_GROUND;
}

void CharacterBody3D::setShape(const physics::CollisionShape &shape)
{
    m_shape = shape;
    rebuild();
}

void CharacterBody3D::setShapeOffset(const glm::vec3 &offset)
{
    m_shapeOffset = offset;
    rebuild();
}

void CharacterBody3D::setMass(float mass)
{
    m_mass = mass;
    if (m_body != nullptr) {
        m_body->setMass(mass);
    }
}

void CharacterBody3D::setMaxSlopeAngle(float radians)
{
    m_maxSlopeAngle = radians;
    if (m_body != nullptr) {
        m_body->setMaxSlopeAngle(radians);
    }
}

void CharacterBody3D::setStepUp(float stepUp)
{
    m_stepUp = stepUp;
    if (m_body != nullptr) {
        m_body->setStepUp(stepUp);
    }
}

void CharacterBody3D::setStepDown(float stepDown)
{
    m_stepDown = stepDown;
    if (m_body != nullptr) {
        m_body->setStepDown(stepDown);
    }
}

void CharacterBody3D::setJumpSpeed(float jumpSpeed)
{
    m_jumpSpeed = jumpSpeed;
}

void CharacterBody3D::serialize(WriteNode node) const
{
    SceneComponent::serialize(node);

    WriteNode body = node.addObject(KEY_BODY);
    physics::CollisionShape_serialize(body, m_shape);
    s_writeVec3(body, KEY_SHAPE_OFFSET, m_shapeOffset);
    body.set(KEY_MASS, m_mass);
    body.set(KEY_MAX_SLOPE_ANGLE, m_maxSlopeAngle);
    body.set(KEY_STEP_UP, m_stepUp);
    body.set(KEY_STEP_DOWN, m_stepDown);
    body.set(KEY_JUMP_SPEED, m_jumpSpeed);
}

void CharacterBody3D::deserialize(ReadNode node)
{
    SceneComponent::deserialize(node);

    ReadNode body = node.child(KEY_BODY);
    if (!body.valid()) {
        return;
    }

    m_shape = physics::CollisionShape_deserialize(body, m_shape);
    m_shapeOffset = s_readVec3(body.child(KEY_SHAPE_OFFSET), m_shapeOffset);
    m_mass = static_cast<float>(body.child(KEY_MASS).asF64(m_mass));
    m_maxSlopeAngle = static_cast<float>(body.child(KEY_MAX_SLOPE_ANGLE).asF64(m_maxSlopeAngle));
    m_stepUp = static_cast<float>(body.child(KEY_STEP_UP).asF64(m_stepUp));
    m_stepDown = static_cast<float>(body.child(KEY_STEP_DOWN).asF64(m_stepDown));
    m_jumpSpeed = static_cast<float>(body.child(KEY_JUMP_SPEED).asF64(m_jumpSpeed));
}

} // namespace Rapture
