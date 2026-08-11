#include "PointLight3D.h"

#include "components/Components.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr uint32_t SHADOW_MAP_SIZE = 1024;

static constexpr std::string_view KEY_POINT_LIGHT = "pointLight";
static constexpr std::string_view KEY_RANGE = "range";

PointLight3D::PointLight3D(Scene &scene, std::string_view name) : Light3D(scene, name)
{
    m_entity.set<PointLightComponent>();
    applyColor();
}

const TypeInfo &PointLight3D::staticType()
{
    static const TypeInfo type("PointLight3D", &Light3D::staticType());
    return type;
}

const TypeInfo &PointLight3D::type() const
{
    return staticType();
}

float PointLight3D::range() const
{
    const auto *light = m_entity.tryRead<PointLightComponent>();
    return light != nullptr ? light->range : 0.0f;
}

void PointLight3D::setRange(float range)
{
    if (!m_entity.has<PointLightComponent>()) {
        return;
    }
    auto light = m_entity.write<PointLightComponent>();

    light->range = range;
    markRenderDataDirty();
}

void PointLight3D::setCastsShadow(bool castsShadow)
{
    if (!m_entity.has<PointLightComponent>()) {
        return;
    }
    auto light = m_entity.write<PointLightComponent>();

    light->setCastsShadow(castsShadow);

    if (!castsShadow) {
        m_entity.tryRemove<ShadowComponent>();
        return;
    }

    if (!m_entity.has<ShadowComponent>()) {
        m_entity.set<ShadowComponent>(SHADOW_MAP_SIZE);
    }
}

void PointLight3D::serialize(WriteNode node) const
{
    Light3D::serialize(node);

    WriteNode light = node.addObject(KEY_POINT_LIGHT);
    light.set(KEY_RANGE, range());
}

void PointLight3D::deserialize(ReadNode node)
{
    Light3D::deserialize(node);

    ReadNode light = node.child(KEY_POINT_LIGHT);
    if (!light.valid()) {
        return;
    }

    setRange(static_cast<float>(light.child(KEY_RANGE).asF64(range())));
}

} // namespace Rapture
