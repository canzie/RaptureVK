#include "Node3D.h"

#include "components/Components.h"
#include "scenes/Scene.h"

namespace Rapture {

static void s_writeVec3(WriteNode node, std::string_view key, const glm::vec3 &value)
{
    WriteNode array = node.addArray(key);
    array.append(static_cast<double>(value.x));
    array.append(static_cast<double>(value.y));
    array.append(static_cast<double>(value.z));
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
    array.append(static_cast<double>(value.x));
    array.append(static_cast<double>(value.y));
    array.append(static_cast<double>(value.z));
    array.append(static_cast<double>(value.w));
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

glm::vec3 Node3D::position() const
{
    const auto *transform = m_entity.tryGetComponent<TransformComponent>();
    return transform != nullptr ? transform->translation() : glm::vec3(0.0f);
}

void Node3D::setPosition(const glm::vec3 &position)
{
    auto *transform = m_entity.tryGetComponent<TransformComponent>();
    if (transform == nullptr) {
        return;
    }

    transform->transforms.setTranslation(position);
    m_entity.markDirty();
}

glm::vec3 Node3D::rotation() const
{
    const auto *transform = m_entity.tryGetComponent<TransformComponent>();
    return transform != nullptr ? transform->rotation() : glm::vec3(0.0f);
}

void Node3D::setRotation(const glm::vec3 &rotation)
{
    auto *transform = m_entity.tryGetComponent<TransformComponent>();
    if (transform == nullptr) {
        return;
    }

    transform->transforms.setRotation(rotation);
    m_entity.markDirty();
}

glm::quat Node3D::rotationQuat() const
{
    const auto *transform = m_entity.tryGetComponent<TransformComponent>();
    return transform != nullptr ? transform->transforms.getRotationQuat() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void Node3D::setRotation(const glm::quat &rotation)
{
    auto *transform = m_entity.tryGetComponent<TransformComponent>();
    if (transform == nullptr) {
        return;
    }

    transform->transforms.setRotation(rotation);
    m_entity.markDirty();
}

glm::vec3 Node3D::scale() const
{
    const auto *transform = m_entity.tryGetComponent<TransformComponent>();
    return transform != nullptr ? transform->scale() : glm::vec3(1.0f);
}

void Node3D::setScale(const glm::vec3 &scale)
{
    auto *transform = m_entity.tryGetComponent<TransformComponent>();
    if (transform == nullptr) {
        return;
    }

    transform->transforms.setScale(scale);
    m_entity.markDirty();
}

glm::mat4 Node3D::localTransform() const
{
    const auto *transform = m_entity.tryGetComponent<TransformComponent>();
    return transform != nullptr ? transform->transformMatrix() : glm::mat4(1.0f);
}

void Node3D::setLocalTransform(const glm::mat4 &transform)
{
    auto *component = m_entity.tryGetComponent<TransformComponent>();
    if (component == nullptr) {
        return;
    }

    component->transforms.setTransform(transform);
    m_entity.markDirty();
}

glm::mat4 Node3D::worldTransform() const
{
    const Node3D *ancestor = findFirstAncestorOfType<Node3D>();
    if (ancestor == nullptr) {
        return localTransform();
    }

    return ancestor->worldTransform() * localTransform();
}

void Node3D::serialize(WriteNode node) const
{
    Instance::serialize(node);

    WriteNode transform = node.addObject("transform");
    s_writeVec3(transform, "translation", position());
    s_writeQuat(transform, "rotation", rotationQuat());
    s_writeVec3(transform, "scale", scale());
}

void Node3D::deserialize(ReadNode node)
{
    Instance::deserialize(node);

    ReadNode transform = node.child("transform");
    if (!transform.valid()) {
        return;
    }

    setPosition(s_readVec3(transform, "translation", position()));
    setRotation(s_readQuat(transform, "rotation", rotationQuat()));
    setScale(s_readVec3(transform, "scale", scale()));
}

} // namespace Rapture
