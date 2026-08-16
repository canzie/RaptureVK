#include "SceneComponent.h"

#include "scene/Scene.h"
#include "scene/instances/SceneObject.h"

namespace Rapture {

SceneComponent::SceneComponent(Scene &scene, std::string_view name) : Instance(scene, name) {}

SceneComponent::~SceneComponent() = default;

const TypeInfo &SceneComponent::staticType()
{
    static const TypeInfo type("SceneComponent", &Instance::staticType());
    return type;
}

const TypeInfo &SceneComponent::type() const
{
    return staticType();
}

ecs::EntityAccessor SceneComponent::ownerEntity() const
{
    if (m_owner == nullptr) {
        return ecs::EntityAccessor();
    }

    return m_owner->accessor();
}

void SceneComponent::onAttach() {}

void SceneComponent::onDetach() {}

void SceneComponent::attachTo(SceneObject *owner)
{
    m_owner = owner;
    onAttach();
}

void SceneComponent::detach()
{
    if (m_owner == nullptr) {
        return;
    }

    setTickEnabled(false);
    onDetach();
    m_owner = nullptr;
}

} // namespace Rapture
