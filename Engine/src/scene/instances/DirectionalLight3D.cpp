#include <rapture/scene/instances/DirectionalLight3D.h>

#include <rapture/scene/Scene.h>
#include <rapture/scene/components/Components.h>

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
}

void DirectionalLight3D::setAtmosphereTransmittance(const glm::vec3 &transmittance)
{
    const auto *current = m_entity.tryRead<DirectionalLightComponent>();
    if (current == nullptr || current->atmosphereTransmittance == transmittance) {
        return;
    }
    auto light = m_entity.write<DirectionalLightComponent>();

    light->atmosphereTransmittance = transmittance;
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

void DirectionalLight3D::setShadowDistance(float distance)
{
    if (!m_entity.has<CascadedShadowComponent>()) {
        return;
    }
    auto shadow = m_entity.write<CascadedShadowComponent>();

    shadow->shadowDistance = distance;
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

    // the cascades are built on construction, so the settings they are built from only take effect if
    // the component is detached and reattached
    const CascadedShadowComponent *current = m_entity.tryRead<CascadedShadowComponent>();
    if (current != nullptr && (current->resolution != shadow.resolution || current->numCascades != shadow.numCascades ||
                               current->lambda != shadow.lambda)) {
        m_entity.tryRemove<CascadedShadowComponent>();
    }

    m_entity.set<CascadedShadowComponent>(shadow);
}

} // namespace Rapture
