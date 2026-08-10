#include "Node3D.h"

#include "components/Components.h"
#include "components/systems/Transforms.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr std::string_view KEY_TRANSFORM = "transform";
static constexpr std::string_view KEY_TRANSLATION = "translation";
static constexpr std::string_view KEY_ROTATION = "rotation";
static constexpr std::string_view KEY_SCALE = "scale";

static void s_writeVec3(WriteNode node, std::string_view key, const glm::vec3 &value)
{
    WriteNode array = node.addArray(key);
    array.append(value.x);
    array.append(value.y);
    array.append(value.z);
}

static glm::vec3 s_readVec3(ReadNode node, std::string_view key, const glm::vec3 &fallback)
{
    ReadNode array = node.child(key);
    if (array.size() != 3) {
        return fallback;
    }

    return glm::vec3(static_cast<float>(array.at(0).asF64(fallback.x)), static_cast<float>(array.at(1).asF64(fallback.y)),
                     static_cast<float>(array.at(2).asF64(fallback.z)));
}

static void s_writeQuat(WriteNode node, std::string_view key, const glm::quat &value)
{
    WriteNode array = node.addArray(key);
    array.append(value.x);
    array.append(value.y);
    array.append(value.z);
    array.append(value.w);
}

static glm::quat s_readQuat(ReadNode node, std::string_view key, const glm::quat &fallback)
{
    ReadNode array = node.child(key);
    if (array.size() != 4) {
        return fallback;
    }

    return glm::quat(static_cast<float>(array.at(3).asF64(fallback.w)), static_cast<float>(array.at(0).asF64(fallback.x)),
                     static_cast<float>(array.at(1).asF64(fallback.y)), static_cast<float>(array.at(2).asF64(fallback.z)));
}

static const glm::mat4 MAT4_IDENTITY = glm::mat4(1.0f);

Node3D::Node3D(Scene &scene, std::string_view name) : Instance(scene, name)
{
    m_entity.setComponent<TransformComponent>();
}

const TypeInfo &Node3D::staticType()
{
    static const TypeInfo type("Node3D", &Instance::staticType());
    return type;
}

const TypeInfo &Node3D::type() const
{
    return staticType();
}

Node3D *Node3D::parentNode() const
{
    return findFirstAncestorOfType<Node3D>();
}

glm::vec3 Node3D::position() const
{
    return transform::translation(localTransform());
}

void Node3D::setPosition(const glm::vec3 &position)
{
    resolveLocal();

    Entity entity = m_entity;
    auto *component = entity.tryGetComponent<TransformComponent>();
    if (component == nullptr) {
        return;
    }

    component->local[3] = glm::vec4(position, 1.0f);
    updateWorldTransform();
}

const glm::vec3 &Node3D::rotation() const
{
    resolveRotationAndScale();
    return m_eulerRotation;
}

void Node3D::setRotation(const glm::vec3 &rotation)
{
    resolveRotationAndScale();
    m_eulerRotation = rotation;
    m_rotation = glm::quat(rotation);
    markRotationAndScaleWritten();
}

const glm::quat &Node3D::rotationQuat() const
{
    resolveRotationAndScale();
    return m_rotation;
}

void Node3D::setRotation(const glm::quat &rotation)
{
    resolveRotationAndScale();
    m_rotation = rotation;
    m_eulerRotation = glm::eulerAngles(rotation);
    markRotationAndScaleWritten();
}

const glm::vec3 &Node3D::scale() const
{
    resolveRotationAndScale();
    return m_scale;
}

void Node3D::setScale(const glm::vec3 &scale)
{
    resolveRotationAndScale();
    m_scale = scale;
    markRotationAndScaleWritten();
}

const glm::mat4 &Node3D::localTransform() const
{
    resolveLocal();

    const auto *component = m_entity.tryGetComponent<TransformComponent>();
    return component != nullptr ? component->local : MAT4_IDENTITY;
}

void Node3D::setLocalTransform(const glm::mat4 &transform)
{
    Entity entity = m_entity;
    auto *component = entity.tryGetComponent<TransformComponent>();
    if (component == nullptr) {
        return;
    }

    component->local = transform;
    markLocalWritten();
}

const glm::mat4 &Node3D::worldTransform() const
{
    const auto *component = m_entity.tryGetComponent<TransformComponent>();
    return component != nullptr ? component->world : MAT4_IDENTITY;
}

void Node3D::setWorldTransform(const glm::mat4 &transform)
{
    const Node3D *parent = parentNode();
    setLocalTransform(parent != nullptr ? transform::toLocal(parent->worldTransform(), transform) : transform);
}

void Node3D::setSimulatedWorldTransform(const glm::mat4 &transform)
{
    Entity entity = m_entity;
    auto *component = entity.tryGetComponent<TransformComponent>();
    if (component == nullptr) {
        return;
    }

    const Node3D *parent = parentNode();
    component->world = transform;
    component->local = parent != nullptr ? transform::toLocal(parent->worldTransform(), transform) : transform;

    m_dirtyMask = TRANSFORM_DIRTY_ROTATION_AND_SCALE;
    entity.markDirty();

    updateDescendantWorldTransforms(*this);
}

void Node3D::markRotationAndScaleWritten()
{
    m_dirtyMask &= ~TRANSFORM_DIRTY_ROTATION_AND_SCALE;
    m_dirtyMask |= TRANSFORM_DIRTY_LOCAL;
    updateWorldTransform();
}

void Node3D::markLocalWritten()
{
    m_dirtyMask &= ~TRANSFORM_DIRTY_LOCAL;
    m_dirtyMask |= TRANSFORM_DIRTY_ROTATION_AND_SCALE;
    updateWorldTransform();
}

void Node3D::updateWorldTransform()
{
    resolveLocal();

    Entity entity = m_entity;
    auto *component = entity.tryGetComponent<TransformComponent>();
    if (component == nullptr) {
        return;
    }

    const Node3D *parent = parentNode();
    component->world = parent != nullptr ? parent->worldTransform() * component->local : component->local;
    entity.markDirty();

    updateDescendantWorldTransforms(*this);
}

void Node3D::onParentChanged()
{
    updateWorldTransform();
}

void Node3D::updateDescendantWorldTransforms(const Instance &parent)
{
    for (const auto &child : parent.children()) {
        Node3D *node = child->as<Node3D>();
        if (node != nullptr) {
            node->updateWorldTransform();
            continue;
        }

        updateDescendantWorldTransforms(*child);
    }
}

void Node3D::resolveRotationAndScale() const
{
    if ((m_dirtyMask & TRANSFORM_DIRTY_ROTATION_AND_SCALE) == 0) {
        return;
    }

    const auto *component = m_entity.tryGetComponent<TransformComponent>();
    if (component != nullptr) {
        m_rotation = transform::rotation(component->local);
        m_eulerRotation = glm::eulerAngles(m_rotation);
        m_scale = transform::scale(component->local);
    }

    m_dirtyMask &= ~TRANSFORM_DIRTY_ROTATION_AND_SCALE;
}

void Node3D::resolveLocal() const
{
    if ((m_dirtyMask & TRANSFORM_DIRTY_LOCAL) == 0) {
        return;
    }

    Entity entity = m_entity;
    auto *component = entity.tryGetComponent<TransformComponent>();
    if (component != nullptr) {
        component->local = transform::compose(transform::translation(component->local), m_rotation, m_scale);
    }

    m_dirtyMask &= ~TRANSFORM_DIRTY_LOCAL;
}

void Node3D::serialize(WriteNode node) const
{
    Instance::serialize(node);

    WriteNode transform = node.addObject(KEY_TRANSFORM);
    s_writeVec3(transform, KEY_TRANSLATION, position());
    s_writeQuat(transform, KEY_ROTATION, rotationQuat());
    s_writeVec3(transform, KEY_SCALE, scale());
}

void Node3D::deserialize(ReadNode node)
{
    Instance::deserialize(node);

    ReadNode transform = node.child(KEY_TRANSFORM);
    if (!transform.valid()) {
        return;
    }

    setPosition(s_readVec3(transform, KEY_TRANSLATION, position()));
    setRotation(s_readQuat(transform, KEY_ROTATION, rotationQuat()));
    setScale(s_readVec3(transform, KEY_SCALE, scale()));
}

} // namespace Rapture
