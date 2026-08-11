#include "SceneComponent.h"

#include "scenes/Scene.h"
#include "scenes/instances/SceneObject.h"

namespace Rapture {

SceneComponent::SceneComponent(Scene &scene, std::string_view name) : Instance(scene, name) {}

SceneComponent::~SceneComponent()
{
    setUpdateEnabled(false);
}

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

void SceneComponent::setUpdateEnabled(bool enabled)
{
    if (isUpdateEnabled() == enabled || scene() == nullptr) {
        return;
    }

    if (enabled) {
        m_updateSlot = scene()->addUpdatingComponent(this);
        return;
    }

    scene()->removeUpdatingComponent(m_updateSlot);
    m_updateSlot = INVALID_UPDATE_SLOT;
}

void SceneComponent::update(float dt)
{
    (void)dt;
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

    onDetach();
    m_owner = nullptr;
}

} // namespace Rapture
