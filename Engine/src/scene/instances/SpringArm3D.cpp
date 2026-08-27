#include "SpringArm3D.h"

#include "scene/Scene.h"
#include "scene/instances/controllers/Controller.h"
#include "scene/systems/Transforms.h"

namespace Rapture {

static constexpr std::string_view KEY_LENGTH = "length";
static constexpr std::string_view KEY_FOLLOWS_CONTROL_ROTATION = "followsControlRotation";

static bool s_isDescendantOf(const SceneObject &object, const SceneObject &ancestor)
{
    for (const SceneObject *walk = object.parent(); walk != nullptr; walk = walk->parent()) {
        if (walk == &ancestor) {
            return true;
        }
    }

    return false;
}

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

void SpringArm3D::setFollowsControlRotation(bool follows)
{
    m_followsControlRotation = follows;
    setTickEnabled(follows);
}

void SpringArm3D::onUpdate(float dt)
{
    (void)dt;

    Controller *controller = scene()->activeController();
    if (controller == nullptr) {
        return;
    }

    SceneObject *subject = controller->possessed();
    if (subject == nullptr || !s_isDescendantOf(*this, *subject)) {
        return;
    }

    // the aim is where the arm ends up in the world, so the parent's turn is taken back out of it
    glm::quat rotation = controller->controlRotation();
    if (Node3D *parent = parentNode()) {
        rotation = glm::inverse(transform::rotation(parent->worldTransform())) * rotation;
    }

    setRotation(rotation);
}

void SpringArm3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    node.set(KEY_LENGTH, m_length);
    node.set(KEY_FOLLOWS_CONTROL_ROTATION, m_followsControlRotation);
}

void SpringArm3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    m_length = static_cast<float>(node.child(KEY_LENGTH).asF64(m_length));
    setFollowsControlRotation(node.child(KEY_FOLLOWS_CONTROL_ROTATION).asBool(m_followsControlRotation));
}

} // namespace Rapture
