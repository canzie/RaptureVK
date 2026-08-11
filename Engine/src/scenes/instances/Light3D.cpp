#include "Light3D.h"

#include "components/Components.h"
#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"

#include <cmath>

namespace Rapture {

static constexpr std::string_view KEY_LIGHT = "light";
static constexpr std::string_view KEY_COLOR = "color";
static constexpr std::string_view KEY_USES_TEMPERATURE = "usesTemperature";
static constexpr std::string_view KEY_TEMPERATURE = "temperature";
static constexpr std::string_view KEY_INTENSITY = "intensity";
static constexpr std::string_view KEY_ACTIVE = "active";
static constexpr std::string_view KEY_MOBILITY = "mobility";
static constexpr std::string_view KEY_CASTS_SHADOW = "castsShadow";
static constexpr std::string_view KEY_SHADOW = "shadow";
static constexpr std::string_view KEY_RESOLUTION = "resolution";

Light3D::Light3D(Scene &scene, std::string_view name) : Node3D(scene, name) {}

const TypeInfo &Light3D::staticType()
{
    static const TypeInfo type("Light3D", &Node3D::staticType());
    return type;
}

const TypeInfo &Light3D::type() const
{
    return staticType();
}

glm::vec3 Light3D::color() const
{
    return m_color;
}

void Light3D::setColor(const glm::vec3 &color)
{
    m_color = color;
    applyColor();
}

float Light3D::intensity() const
{
    const LightComponent *light = Light_tryReadLight(m_entity);
    return light != nullptr ? light->intensity : 1.0f;
}

void Light3D::setIntensity(float intensity)
{
    LightComponent *light = Light_tryWriteLight(m_entity, ecs::ChannelBit(CHANNEL_LIGHT_PARAMS));
    if (light == nullptr) {
        return;
    }

    light->setIntensity(intensity);
    markRenderDataDirty();
}

bool Light3D::usesTemperature() const
{
    return m_usesTemperature;
}

void Light3D::setUsesTemperature(bool usesTemperature)
{
    m_usesTemperature = usesTemperature;
    applyColor();
}

float Light3D::temperature() const
{
    return m_temperature;
}

void Light3D::setTemperature(float temperature)
{
    m_temperature = temperature;
    applyColor();
}

glm::vec3 Light3D::kelvinToRgb(float kelvin)
{
    float t = glm::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;

    float r;
    float g;
    float b;

    if (t <= 66.0f) {
        r = 1.0f;
    } else {
        r = glm::clamp(329.698727446f * std::pow(t - 60.0f, -0.1332047592f) / 255.0f, 0.0f, 1.0f);
    }

    if (t <= 66.0f) {
        g = glm::clamp((99.4708025861f * std::log(t) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
    } else {
        g = glm::clamp(288.1221695283f * std::pow(t - 60.0f, -0.0755148492f) / 255.0f, 0.0f, 1.0f);
    }

    if (t >= 66.0f) {
        b = 1.0f;
    } else if (t <= 19.0f) {
        b = 0.0f;
    } else {
        b = glm::clamp((138.5177312231f * std::log(t - 10.0f) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
    }

    return glm::vec3(r, g, b);
}

bool Light3D::isActive() const
{
    const LightComponent *light = Light_tryReadLight(m_entity);
    return light != nullptr ? light->isActive : false;
}

void Light3D::setActive(bool active)
{
    LightComponent *light = Light_tryWriteLight(m_entity, ecs::ChannelBit(CHANNEL_LIGHT_PARAMS));
    if (light == nullptr) {
        return;
    }

    light->setActive(active);
    markRenderDataDirty();
}

Mobility Light3D::mobility() const
{
    const LightComponent *light = Light_tryReadLight(m_entity);
    return light != nullptr ? light->mobility : MOBILITY_STATIC;
}

void Light3D::setMobility(Mobility mobility)
{
    if (Light_tryReadLight(m_entity) == nullptr) {
        return;
    }

    scene()->getRenderData()->setLightMobility(m_entity.getEntity(), mobility);
    markRenderDataDirty();
}

bool Light3D::castsShadow() const
{
    const LightComponent *light = Light_tryReadLight(m_entity);
    return light != nullptr ? light->castsShadow : false;
}

void Light3D::applyColor()
{
    LightComponent *light = Light_tryWriteLight(m_entity, ecs::ChannelBit(CHANNEL_LIGHT_PARAMS));
    if (light == nullptr) {
        return;
    }

    light->setColor(m_usesTemperature ? m_color * kelvinToRgb(m_temperature) : m_color);
    markRenderDataDirty();
}

void Light3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode light = node.addObject(KEY_LIGHT);
    WriteNode color = light.addArray(KEY_COLOR);
    color.append(m_color.x);
    color.append(m_color.y);
    color.append(m_color.z);
    light.set(KEY_USES_TEMPERATURE, m_usesTemperature);
    light.set(KEY_TEMPERATURE, m_temperature);
    light.set(KEY_INTENSITY, intensity());
    light.set(KEY_ACTIVE, isActive());
    light.set(KEY_MOBILITY, static_cast<uint64_t>(mobility()));
    light.set(KEY_CASTS_SHADOW, castsShadow());

    const ShadowComponent *shadow = m_entity.tryRead<ShadowComponent>();
    if (shadow == nullptr) {
        return;
    }

    WriteNode shadowNode = node.addObject(KEY_SHADOW);
    shadowNode.set(KEY_RESOLUTION, static_cast<uint64_t>(shadow->resolution));
    shadowNode.set(KEY_ACTIVE, shadow->isActive);
    shadowNode.set(KEY_MOBILITY, static_cast<uint64_t>(shadow->mobility));
}

void Light3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode light = node.child(KEY_LIGHT);
    if (!light.valid()) {
        return;
    }

    ReadNode color = light.child(KEY_COLOR);
    if (color.size() == 3) {
        m_color = glm::vec3(static_cast<float>(color.at(0).asF64(m_color.x)), static_cast<float>(color.at(1).asF64(m_color.y)),
                            static_cast<float>(color.at(2).asF64(m_color.z)));
    }

    m_usesTemperature = light.child(KEY_USES_TEMPERATURE).asBool(m_usesTemperature);
    m_temperature = static_cast<float>(light.child(KEY_TEMPERATURE).asF64(m_temperature));
    applyColor();

    setIntensity(static_cast<float>(light.child(KEY_INTENSITY).asF64(intensity())));
    setActive(light.child(KEY_ACTIVE).asBool(isActive()));
    setMobility(static_cast<Mobility>(light.child(KEY_MOBILITY).asU64(static_cast<uint64_t>(mobility()))));

    // the shadow map is built from the shadow component's construction signal, so the settings go on
    // the component before shadow casting attaches it
    bool shouldCastShadow = light.child(KEY_CASTS_SHADOW).asBool(castsShadow());
    if (shouldCastShadow) {
        applyShadowSettings(node);
    }
    setCastsShadow(shouldCastShadow);
}

void Light3D::applyShadowSettings(ReadNode node)
{
    ReadNode shadowNode = node.child(KEY_SHADOW);
    if (!shadowNode.valid()) {
        return;
    }

    ShadowComponent shadow;
    shadow.resolution = static_cast<uint32_t>(shadowNode.child(KEY_RESOLUTION).asU64(shadow.resolution));
    shadow.isActive = shadowNode.child(KEY_ACTIVE).asBool(shadow.isActive);
    shadow.mobility = static_cast<Mobility>(shadowNode.child(KEY_MOBILITY).asU64(static_cast<uint64_t>(shadow.mobility)));

    m_entity.set<ShadowComponent>(shadow);
}

} // namespace Rapture
