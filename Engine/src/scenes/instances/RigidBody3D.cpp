#include "RigidBody3D.h"

#include "components/systems/Transforms.h"
#include "logging/Log.h"
#include "physics/PhysicsSystem.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_BODY = "body";
static constexpr std::string_view KEY_SHAPE = "shape";
static constexpr std::string_view KEY_HALF_EXTENTS = "halfExtents";
static constexpr std::string_view KEY_RADIUS = "radius";
static constexpr std::string_view KEY_HALF_HEIGHT = "halfHeight";
static constexpr std::string_view KEY_MOTION_TYPE = "motionType";
static constexpr std::string_view KEY_FRICTION = "friction";
static constexpr std::string_view KEY_RESTITUTION = "restitution";

static glm::mat4 s_rigidPart(const glm::mat4 &matrix)
{
    return transform::compose(transform::translation(matrix), transform::rotation(matrix), glm::vec3(1.0f));
}

static void s_writeShape(WriteNode node, const PhysicsShape &shape)
{
    node.set(KEY_SHAPE, PhysicsShape_toString(PhysicsShape_typeOf(shape)));

    if (const auto *box = std::get_if<PhysicsBoxShape>(&shape)) {
        WriteNode extents = node.addArray(KEY_HALF_EXTENTS);
        extents.append(box->halfExtents.x);
        extents.append(box->halfExtents.y);
        extents.append(box->halfExtents.z);
        return;
    }

    if (const auto *sphere = std::get_if<PhysicsSphereShape>(&shape)) {
        node.set(KEY_RADIUS, sphere->radius);
        return;
    }

    if (const auto *capsule = std::get_if<PhysicsCapsuleShape>(&shape)) {
        node.set(KEY_RADIUS, capsule->radius);
        node.set(KEY_HALF_HEIGHT, capsule->halfHeight);
    }
}

static PhysicsShape s_readShape(ReadNode node, const PhysicsShape &fallback)
{
    const PhysicsShapeType type = PhysicsShape_fromString(node.child(KEY_SHAPE).asString(), PhysicsShape_typeOf(fallback));

    if (type == PHYSICS_SHAPE_SPHERE) {
        return PhysicsSphereShape{static_cast<float>(node.child(KEY_RADIUS).asF64(0.5))};
    }

    if (type == PHYSICS_SHAPE_CAPSULE) {
        return PhysicsCapsuleShape{static_cast<float>(node.child(KEY_HALF_HEIGHT).asF64(0.5)),
                                   static_cast<float>(node.child(KEY_RADIUS).asF64(0.5))};
    }

    ReadNode extents = node.child(KEY_HALF_EXTENTS);
    if (extents.size() != 3) {
        return PhysicsBoxShape{};
    }

    return PhysicsBoxShape{glm::vec3(static_cast<float>(extents.at(0).asF64(0.5)), static_cast<float>(extents.at(1).asF64(0.5)),
                                     static_cast<float>(extents.at(2).asF64(0.5)))};
}

RigidBody3D::RigidBody3D(Scene &scene, std::string_view name, Node3D *host) : Node3D(scene, name), m_host(host)
{
    rebuild();
}

RigidBody3D::~RigidBody3D()
{
    PhysicsSystem *physics = scene() != nullptr ? scene()->physics() : nullptr;
    if (physics != nullptr && m_bodyId.isValid()) {
        physics->removeRigidBody(m_bodyId);
    }
}

const TypeInfo &RigidBody3D::staticType()
{
    static const TypeInfo type("RigidBody3D", &Node3D::staticType());
    return type;
}

const TypeInfo &RigidBody3D::type() const
{
    return staticType();
}

Node3D *RigidBody3D::target() const
{
    return m_host != nullptr ? m_host : const_cast<RigidBody3D *>(this);
}

glm::mat4 RigidBody3D::offsetTransform() const
{
    if (m_host == nullptr) {
        return glm::mat4(1.0f);
    }

    const glm::mat4 &local = localTransform();
    const glm::vec3 hostScale = transform::scale(m_host->worldTransform());
    return transform::compose(transform::translation(local) * hostScale, transform::rotation(local), glm::vec3(1.0f));
}

glm::mat4 RigidBody3D::bodyTransform(const glm::mat4 &offset) const
{
    return s_rigidPart(target()->worldTransform()) * offset;
}

void RigidBody3D::rebuild()
{
    PhysicsSystem *physics = scene() != nullptr ? scene()->physics() : nullptr;
    if (physics == nullptr) {
        RP_CORE_ERROR("'{}' cannot join a scene that has no simulation", name());
        return;
    }

    if (m_bodyId.isValid()) {
        physics->removeRigidBody(m_bodyId);
        m_bodyId = PhysicsBodyId();
    }

    const glm::mat4 offset = offsetTransform();
    m_inverseOffset = glm::inverse(offset);

    const glm::mat4 body = bodyTransform(offset);

    RigidBodyConfig config;
    config.shape = m_shape;
    config.position = transform::translation(body);
    config.rotation = transform::rotation(body);
    config.motionType = m_motionType;
    config.friction = m_friction;
    config.restitution = m_restitution;
    config.startActive = m_startActive;

    m_bodyId = physics->createRigidBody(config, reinterpret_cast<uint64_t>(this));
}

void RigidBody3D::applySimulatedTransform(const glm::vec3 &position, const glm::quat &rotation)
{
    Node3D *node = target();
    const glm::mat4 world = transform::compose(position, rotation, glm::vec3(1.0f)) * m_inverseOffset;

    node->setSimulatedWorldTransform(
        transform::compose(transform::translation(world), transform::rotation(world), node->scale()));
}

void RigidBody3D::setShape(const PhysicsShape &shape)
{
    m_shape = shape;
    rebuild();
}

void RigidBody3D::setMotionType(PhysicsMotionType motionType)
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
    rebuild();
}

void RigidBody3D::setRestitution(float restitution)
{
    m_restitution = restitution;
    rebuild();
}

void RigidBody3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode body = node.addObject(KEY_BODY);
    s_writeShape(body, m_shape);
    body.set(KEY_MOTION_TYPE, PhysicsMotionType_toString(m_motionType));
    body.set(KEY_FRICTION, m_friction);
    body.set(KEY_RESTITUTION, m_restitution);
}

void RigidBody3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode body = node.child(KEY_BODY);
    if (!body.valid()) {
        return;
    }

    m_shape = s_readShape(body, m_shape);
    m_motionType = static_cast<PhysicsMotionType>(body.child(KEY_MOTION_TYPE).asU64(static_cast<uint64_t>(m_motionType)));
    m_friction = static_cast<float>(body.child(KEY_FRICTION).asF64(m_friction));
    m_restitution = static_cast<float>(body.child(KEY_RESTITUTION).asF64(m_restitution));

    rebuild();
}

} // namespace Rapture
