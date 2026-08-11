#include "DirectionalLight3D.h"

#include "components/Components.h"
#include "scenes/Scene.h"

namespace Rapture {

static constexpr uint32_t CASCADED_SHADOW_MAP_SIZE = 2048;
static constexpr uint8_t CASCADE_COUNT = 4;
static constexpr float CASCADE_LAMBDA = 0.8f;

static constexpr std::string_view KEY_DIRECTIONAL_LIGHT = "directionalLight";
static constexpr std::string_view KEY_ATMOSPHERE_SUN = "atmosphereSun";
static constexpr std::string_view KEY_CASCADED_SHADOW = "cascadedShadow";
static constexpr std::string_view KEY_RESOLUTION = "resolution";
static constexpr std::string_view KEY_NUM_CASCADES = "numCascades";
static constexpr std::string_view KEY_LAMBDA = "lambda";
static constexpr std::string_view KEY_SHADOW_DISTANCE = "shadowDistance";
static constexpr std::string_view KEY_ACTIVE = "active";
static constexpr std::string_view KEY_MOBILITY = "mobility";

DirectionalLight3D::DirectionalLight3D(Scene &scene, std::string_view name) : Light3D(scene, name)
{
    m_entity.set<DirectionalLightComponent>();
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
    const auto *light = m_entity.tryRead<DirectionalLightComponent>();
    return light != nullptr ? light->atmosphereSunLight : false;
}

void DirectionalLight3D::setAtmosphereSun(bool atmosphereSun)
{
    if (!m_entity.has<DirectionalLightComponent>()) {
        return;
    }
    auto light = m_entity.write<DirectionalLightComponent>();

    light->atmosphereSunLight = atmosphereSun;
    markRenderDataDirty();
}

void DirectionalLight3D::setCastsShadow(bool castsShadow)
{
    if (!m_entity.has<DirectionalLightComponent>()) {
        return;
    }
    auto light = m_entity.write<DirectionalLightComponent>();

    light->setCastsShadow(castsShadow);

    if (!castsShadow) {
        m_entity.tryRemove<CascadedShadowComponent>();
        return;
    }

    if (!m_entity.has<CascadedShadowComponent>()) {
        m_entity.set<CascadedShadowComponent>(CASCADED_SHADOW_MAP_SIZE, CASCADE_COUNT, CASCADE_LAMBDA);
    }
}

void DirectionalLight3D::serialize(WriteNode node) const
{
    Light3D::serialize(node);

    WriteNode light = node.addObject(KEY_DIRECTIONAL_LIGHT);
    light.set(KEY_ATMOSPHERE_SUN, isAtmosphereSun());

    const CascadedShadowComponent *shadow = m_entity.tryRead<CascadedShadowComponent>();
    if (shadow == nullptr) {
        return;
    }

    WriteNode shadowNode = node.addObject(KEY_CASCADED_SHADOW);
    shadowNode.set(KEY_RESOLUTION, static_cast<uint64_t>(shadow->resolution));
    shadowNode.set(KEY_NUM_CASCADES, static_cast<uint64_t>(shadow->numCascades));
    shadowNode.set(KEY_LAMBDA, shadow->lambda);
    shadowNode.set(KEY_SHADOW_DISTANCE, shadow->shadowDistance);
    shadowNode.set(KEY_ACTIVE, shadow->isActive);
    shadowNode.set(KEY_MOBILITY, static_cast<uint64_t>(shadow->mobility));
}

void DirectionalLight3D::deserialize(ReadNode node)
{
    Light3D::deserialize(node);

    ReadNode light = node.child(KEY_DIRECTIONAL_LIGHT);
    if (!light.valid()) {
        return;
    }

    setAtmosphereSun(light.child(KEY_ATMOSPHERE_SUN).asBool(isAtmosphereSun()));
}

void DirectionalLight3D::applyShadowSettings(ReadNode node)
{
    ReadNode shadowNode = node.child(KEY_CASCADED_SHADOW);
    if (!shadowNode.valid()) {
        return;
    }

    CascadedShadowComponent shadow;
    shadow.resolution = static_cast<uint32_t>(shadowNode.child(KEY_RESOLUTION).asU64(shadow.resolution));
    shadow.numCascades = static_cast<uint8_t>(shadowNode.child(KEY_NUM_CASCADES).asU64(shadow.numCascades));
    shadow.lambda = static_cast<float>(shadowNode.child(KEY_LAMBDA).asF64(shadow.lambda));
    shadow.shadowDistance = static_cast<float>(shadowNode.child(KEY_SHADOW_DISTANCE).asF64(shadow.shadowDistance));
    shadow.isActive = shadowNode.child(KEY_ACTIVE).asBool(shadow.isActive);
    shadow.mobility = static_cast<Mobility>(shadowNode.child(KEY_MOBILITY).asU64(static_cast<uint64_t>(shadow.mobility)));

    m_entity.set<CascadedShadowComponent>(shadow);
}

} // namespace Rapture
