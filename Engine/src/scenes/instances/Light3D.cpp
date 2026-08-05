#include "Light3D.h"

#include "components/Components.h"
#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"

#include <cmath>

namespace Rapture {

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
    const LightComponent *light = Light_tryGetLight(m_entity);
    return light != nullptr ? light->intensity : 1.0f;
}

void Light3D::setIntensity(float intensity)
{
    LightComponent *light = Light_tryGetLight(m_entity);
    if (light == nullptr) {
        return;
    }

    light->setIntensity(intensity);
    m_entity.markDirty();
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
    const LightComponent *light = Light_tryGetLight(m_entity);
    return light != nullptr ? light->isActive : false;
}

void Light3D::setActive(bool active)
{
    LightComponent *light = Light_tryGetLight(m_entity);
    if (light == nullptr) {
        return;
    }

    light->setActive(active);
    m_entity.markDirty();
}

Mobility Light3D::mobility() const
{
    const LightComponent *light = Light_tryGetLight(m_entity);
    return light != nullptr ? light->mobility : MOBILITY_STATIC;
}

void Light3D::setMobility(Mobility mobility)
{
    if (Light_tryGetLight(m_entity) == nullptr) {
        return;
    }

    scene()->getRenderData()->setLightMobility(m_entity.getID(), mobility);
    m_entity.markDirty();
}

bool Light3D::castsShadow() const
{
    const LightComponent *light = Light_tryGetLight(m_entity);
    return light != nullptr ? light->castsShadow : false;
}

void Light3D::applyColor()
{
    LightComponent *light = Light_tryGetLight(m_entity);
    if (light == nullptr) {
        return;
    }

    light->setColor(m_usesTemperature ? m_color * kelvinToRgb(m_temperature) : m_color);
    m_entity.markDirty();
}

void Light3D::serialize(WriteNode node) const
{
    Node3D::serialize(node);

    WriteNode light = node.addObject("light");
    WriteNode color = light.addArray("color");
    color.append(m_color.x);
    color.append(m_color.y);
    color.append(m_color.z);
    light.set("usesTemperature", m_usesTemperature);
    light.set("temperature", m_temperature);
    light.set("intensity", intensity());
    light.set("active", isActive());
    light.set("mobility", static_cast<uint64_t>(mobility()));
    light.set("castsShadow", castsShadow());

    const ShadowComponent *shadow = m_entity.tryGetComponent<ShadowComponent>();
    if (shadow == nullptr) {
        return;
    }

    WriteNode shadowNode = node.addObject("shadow");
    shadowNode.set("resolution", static_cast<uint64_t>(shadow->resolution));
    shadowNode.set("active", shadow->isActive);
    shadowNode.set("mobility", static_cast<uint64_t>(shadow->mobility));
}

void Light3D::deserialize(ReadNode node)
{
    Node3D::deserialize(node);

    ReadNode light = node.child("light");
    if (!light.valid()) {
        return;
    }

    ReadNode color = light.child("color");
    if (color.size() == 3) {
        m_color = glm::vec3(static_cast<float>(color.at(0).asF64(m_color.x)), static_cast<float>(color.at(1).asF64(m_color.y)),
                            static_cast<float>(color.at(2).asF64(m_color.z)));
    }

    m_usesTemperature = light.child("usesTemperature").asBool(m_usesTemperature);
    m_temperature = static_cast<float>(light.child("temperature").asF64(m_temperature));
    applyColor();

    setIntensity(static_cast<float>(light.child("intensity").asF64(intensity())));
    setActive(light.child("active").asBool(isActive()));
    setMobility(static_cast<Mobility>(light.child("mobility").asU64(static_cast<uint64_t>(mobility()))));
    setCastsShadow(light.child("castsShadow").asBool(castsShadow()));
}

} // namespace Rapture
