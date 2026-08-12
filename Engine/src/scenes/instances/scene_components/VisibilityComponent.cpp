#include "VisibilityComponent.h"

namespace Rapture {

static constexpr std::string_view KEY_IN_OUTLINER = "inOutliner";

VisibilityComponent::VisibilityComponent(Scene &scene, std::string_view name) : SceneComponent(scene, name) {}

const TypeInfo &VisibilityComponent::staticType()
{
    static const TypeInfo type("VisibilityComponent", &SceneComponent::staticType());
    return type;
}

const TypeInfo &VisibilityComponent::type() const
{
    return staticType();
}

void VisibilityComponent::serialize(WriteNode node) const
{
    SceneComponent::serialize(node);

    node.set(KEY_IN_OUTLINER, inOutliner);
}

void VisibilityComponent::deserialize(ReadNode node)
{
    SceneComponent::deserialize(node);

    inOutliner = node.child(KEY_IN_OUTLINER).asBool(inOutliner);
}

} // namespace Rapture
