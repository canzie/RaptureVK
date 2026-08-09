#include "SpringArm3D.h"

namespace Rapture {

static constexpr std::string_view KEY_LENGTH = "length";

SpringArm3D::SpringArm3D(Scene &scene, std::string_view name) : Node3D(scene, name) {}

const TypeInfo &SpringArm3D::staticType()
{
    static const TypeInfo type("SpringArm3D", &Node3D::staticType());
    return type;
}

const TypeInfo &SpringArm3D::type() const
{
    return staticType();
}

void SpringArm3D::setLength(float length)
{
    m_length = length;
    applyLength();
}

void SpringArm3D::applyLength()
{
    for (const auto &child : children()) {
        Node3D *node = child->as<Node3D>();
        if (node == nullptr) {
            continue;
        }
        node->setPosition(glm::vec3(0.0f, 0.0f, m_length));
    }
}

void SpringArm3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    node.set(KEY_LENGTH, m_length);
}

void SpringArm3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    m_length = static_cast<float>(node.child(KEY_LENGTH).asF64(m_length));
}

} // namespace Rapture
