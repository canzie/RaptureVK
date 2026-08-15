#include "RigidBody3D.h"

#include "components/systems/Transforms.h"
#include "logging/Log.h"
#include "physics/Serialization.h"
#include "scenes/Scene.h"
#include "scenes/instances/Node3D.h"

namespace Rapture {

static constexpr std::string_view KEY_BODY = "body";
static constexpr std::string_view KEY_MOTION_TYPE = "motionType";
static constexpr std::string_view KEY_FRICTION = "friction";
static constexpr std::string_view KEY_RESTITUTION = "restitution";
static constexpr std::string_view KEY_LOCAL_TRANSFORM = "localTransform";

static glm::mat4 s_rigidPart(const glm::mat4 &matrix)
{
    return transform::compose(transform::translation(matrix), transform::rotation(matrix), glm::vec3(1.0f));
}

static void s_writeMatrix(WriteNode node, std::string_view key, const glm::mat4 &matrix)
{
    WriteNode values = node.addArray(key);
    const float *source = &matrix[0][0];
    for (size_t i = 0; i < 16; i++) {
        values.append(source[i]);
    }
}

static glm::mat4 s_readMatrix(ReadNode node, const glm::mat4 &fallback)
{
    if (node.size() != 16) {
        return fallback;
    }

    glm::mat4 matrix(1.0f);
    float *target = &matrix[0][0];
    for (size_t i = 0; i < 16; i++) {
        target[i] = static_cast<float>(node.at(i).asF64(0.0));
    }

    return matrix;
}

RigidBody3D::RigidBody3D(Scene &scene, std::string_view name) : PhysicsBody3D(scene, name) {}

RigidBody3D::~RigidBody3D()
{
    releaseBody();
}

const TypeInfo &RigidBody3D::staticType()
{
    static const TypeInfo type("RigidBody3D", &PhysicsBody3D::staticType());
    return type;
}

const TypeInfo &RigidBody3D::type() const
{
    return staticType();
}

Node3D *RigidBody3D::node() const
{
    SceneObject *object = owner();
    return object != nullptr ? object->as<Node3D>() : nullptr;
}

glm::mat4 RigidBody3D::scaledLocalTransform() const
{
    const Node3D *target = node();
    if (target == nullptr) {
        return s_rigidPart(m_localTransform);
    }

    const glm::vec3 worldScale = transform::scale(target->worldTransform());
    return transform::compose(transform::translation(m_localTransform) * worldScale, transform::rotation(m_localTransform),
                              glm::vec3(1.0f));
}

glm::mat4 RigidBody3D::bodyTransform(const glm::mat4 &local) const
{
    const Node3D *target = node();
    if (target == nullptr) {
        return local;
    }

    return s_rigidPart(target->worldTransform()) * local;
}

void RigidBody3D::releaseBody()
{
    m_body.reset();
}

void RigidBody3D::onAttach()
{
    if (node() == nullptr) {
        RP_CORE_ERROR("'{}' needs an object with a place in the world", name());
        return;
    }

    rebuild();
}

void RigidBody3D::onDetach()
{
    releaseBody();
}

void RigidBody3D::rebuild()
{
    if (node() == nullptr) {
        return;
    }

    // a rebuild is a new body standing where the old one did, so the motion it had is carried over
    // rather than dropping back to rest
    const glm::vec3 carriedVelocity = m_body != nullptr ? m_body->linearVelocity() : glm::vec3(0.0f);

    releaseBody();

    const glm::mat4 local = scaledLocalTransform();
    m_inverseLocal = glm::inverse(local);

    const glm::mat4 body = bodyTransform(local);

    physics::RigidBodyConfig config;
    config.shape = physics::CollisionShape_scaled(m_shape, transform::scale(node()->worldTransform()));
    config.position = transform::translation(body);
    config.rotation = transform::rotation(body);
    config.motionType = m_motionType;
    config.friction = m_friction;
    config.restitution = m_restitution;
    config.startActive = m_startActive;

    m_body = createRigidBody(config);
    if (m_body == nullptr) {
        return;
    }

    m_body->setLinearVelocity(carriedVelocity);
}

void RigidBody3D::setVelocity(const glm::vec3 &velocity)
{
    if (m_body == nullptr) {
        return;
    }

    m_body->setLinearVelocity(velocity);
}

void RigidBody3D::applySimulatedTransform(const glm::vec3 &position, const glm::quat &rotation)
{
    Node3D *target = node();
    if (target == nullptr) {
        return;
    }

    const glm::mat4 world = transform::compose(position, rotation, glm::vec3(1.0f)) * m_inverseLocal;

    target->setSimulatedWorldTransform(
        transform::compose(transform::translation(world), transform::rotation(world), target->scale()));
}

void RigidBody3D::setShape(const physics::CollisionShape &shape)
{
    m_shape = shape;
    rebuild();
}

void RigidBody3D::setLocalTransform(const glm::mat4 &transform)
{
    m_localTransform = transform;
    rebuild();
}

void RigidBody3D::setMotionType(physics::MotionType motionType)
{
    if (m_motionType == motionType) {
        return;
    }

    m_motionType = motionType;
    rebuild();
}

void RigidBody3D::setFriction(float friction)
{
    m_friction = friction;
    if (m_body != nullptr) {
        m_body->setFriction(friction);
    }
}

void RigidBody3D::setRestitution(float restitution)
{
    m_restitution = restitution;
    if (m_body != nullptr) {
        m_body->setRestitution(restitution);
    }
}

void RigidBody3D::serialize(WriteNode node) const
{
    SceneComponent::serialize(node);

    WriteNode body = node.addObject(KEY_BODY);
    physics::CollisionShape_serialize(body, m_shape);
    s_writeMatrix(body, KEY_LOCAL_TRANSFORM, m_localTransform);
    body.set(KEY_MOTION_TYPE, physics::MotionType_toString(m_motionType));
    body.set(KEY_FRICTION, m_friction);
    body.set(KEY_RESTITUTION, m_restitution);
}

void RigidBody3D::deserialize(ReadNode node)
{
    SceneComponent::deserialize(node);

    ReadNode body = node.child(KEY_BODY);
    if (!body.valid()) {
        return;
    }

    m_shape = physics::CollisionShape_deserialize(body, m_shape);
    m_localTransform = s_readMatrix(body.child(KEY_LOCAL_TRANSFORM), m_localTransform);
    m_motionType = physics::MotionType_fromString(body.child(KEY_MOTION_TYPE).asString(), m_motionType);
    m_friction = static_cast<float>(body.child(KEY_FRICTION).asF64(m_friction));
    m_restitution = static_cast<float>(body.child(KEY_RESTITUTION).asF64(m_restitution));
}

} // namespace Rapture
