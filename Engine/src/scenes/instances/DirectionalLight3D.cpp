#include "DirectionalLight3D.h"

#include "components/Components.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr uint32_t CASCADED_SHADOW_MAP_SIZE = 2048;
static constexpr uint8_t CASCADE_COUNT = 4;
static constexpr float CASCADE_LAMBDA = 0.8f;

DirectionalLight3D::DirectionalLight3D(Scene &scene, std::string_view name) : Light3D(scene, name)
{
    m_entity.setComponent<DirectionalLightComponent>();
    applyColor();
}

const TypeInfo &DirectionalLight3D::staticType()
{
    static const TypeInfo type("DirectionalLight3D", &Light3D::staticType());
    return type;
}

const TypeInfo &DirectionalLight3D::type() const
{
    return staticType();
}

bool DirectionalLight3D::isAtmosphereSun() const
{
    const auto *light = m_entity.tryGetComponent<DirectionalLightComponent>();
    return light != nullptr ? light->atmosphereSunLight : false;
}

void DirectionalLight3D::setAtmosphereSun(bool atmosphereSun)
{
    auto *light = m_entity.tryGetComponent<DirectionalLightComponent>();
    if (light == nullptr) {
        return;
    }

    light->atmosphereSunLight = atmosphereSun;
    m_entity.markDirty();
}

void DirectionalLight3D::setCastsShadow(bool castsShadow)
{
    auto *light = m_entity.tryGetComponent<DirectionalLightComponent>();
    if (light == nullptr) {
        return;
    }

    light->setCastsShadow(castsShadow);

    if (!castsShadow) {
        m_entity.tryRemoveComponent<CascadedShadowComponent>();
        return;
    }

    if (!m_entity.hasComponent<CascadedShadowComponent>()) {
        m_entity.setComponent<CascadedShadowComponent>(CASCADED_SHADOW_MAP_SIZE, CASCADE_COUNT, CASCADE_LAMBDA);
    }
}

void DirectionalLight3D::serialize(WriteNode node) const
{
    Light3D::serialize(node);

    WriteNode light = node.addObject("directionalLight");
    light.set("atmosphereSun", isAtmosphereSun());
}

void DirectionalLight3D::deserialize(ReadNode node)
{
    Light3D::deserialize(node);

    ReadNode light = node.child("directionalLight");
    if (!light.valid()) {
        return;
    }

    setAtmosphereSun(light.child("atmosphereSun").asBool(isAtmosphereSun()));
}

} // namespace Rapture
