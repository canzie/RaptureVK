#include "SpotLight3D.h"

#include "components/Components.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr uint32_t SHADOW_MAP_SIZE = 1024;

static constexpr std::string_view KEY_SPOT_LIGHT = "spotLight";
static constexpr std::string_view KEY_RANGE = "range";
static constexpr std::string_view KEY_INNER_CONE_ANGLE = "innerConeAngle";
static constexpr std::string_view KEY_OUTER_CONE_ANGLE = "outerConeAngle";

SpotLight3D::SpotLight3D(Scene &scene, std::string_view name) : Light3D(scene, name)
{
    m_entity.setComponent<SpotLightComponent>();
    applyColor();
}

const TypeInfo &SpotLight3D::staticType()
{
    static const TypeInfo type("SpotLight3D", &Light3D::staticType());
    return type;
}

const TypeInfo &SpotLight3D::type() const
{
    return staticType();
}

float SpotLight3D::range() const
{
    const auto *light = m_entity.tryGetComponent<SpotLightComponent>();
    return light != nullptr ? light->range : 0.0f;
}

void SpotLight3D::setRange(float range)
{
    auto *light = m_entity.tryGetComponent<SpotLightComponent>();
    if (light == nullptr) {
        return;
    }

    light->range = range;
    m_entity.markDirty();
}

float SpotLight3D::innerConeAngle() const
{
    const auto *light = m_entity.tryGetComponent<SpotLightComponent>();
    return light != nullptr ? light->innerConeAngle : 0.0f;
}

void SpotLight3D::setInnerConeAngle(float radians)
{
    auto *light = m_entity.tryGetComponent<SpotLightComponent>();
    if (light == nullptr) {
        return;
    }

    light->innerConeAngle = radians;
    m_entity.markDirty();
}

float SpotLight3D::outerConeAngle() const
{
    const auto *light = m_entity.tryGetComponent<SpotLightComponent>();
    return light != nullptr ? light->outerConeAngle : 0.0f;
}

void SpotLight3D::setOuterConeAngle(float radians)
{
    auto *light = m_entity.tryGetComponent<SpotLightComponent>();
    if (light == nullptr) {
        return;
    }

    light->outerConeAngle = radians;
    m_entity.markDirty();
}

void SpotLight3D::setCastsShadow(bool castsShadow)
{
    auto *light = m_entity.tryGetComponent<SpotLightComponent>();
    if (light == nullptr) {
        return;
    }

    light->setCastsShadow(castsShadow);

    if (!castsShadow) {
        m_entity.tryRemoveComponent<ShadowComponent>();
        return;
    }

    if (!m_entity.hasComponent<ShadowComponent>()) {
        m_entity.setComponent<ShadowComponent>(SHADOW_MAP_SIZE);
    }
}

void SpotLight3D::serialize(WriteNode node) const
{
    Light3D::serialize(node);

    WriteNode light = node.addObject(KEY_SPOT_LIGHT);
    light.set(KEY_RANGE, range());
    light.set(KEY_INNER_CONE_ANGLE, innerConeAngle());
    light.set(KEY_OUTER_CONE_ANGLE, outerConeAngle());
}

void SpotLight3D::deserialize(ReadNode node)
{
    Light3D::deserialize(node);

    ReadNode light = node.child(KEY_SPOT_LIGHT);
    if (!light.valid()) {
        return;
    }

    setRange(static_cast<float>(light.child(KEY_RANGE).asF64(range())));
    setInnerConeAngle(static_cast<float>(light.child(KEY_INNER_CONE_ANGLE).asF64(innerConeAngle())));
    setOuterConeAngle(static_cast<float>(light.child(KEY_OUTER_CONE_ANGLE).asF64(outerConeAngle())));
}

} // namespace Rapture
