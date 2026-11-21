#include <rapture/assets/textures/ATexture.h>
#include <rapture/scene/instances/Environment.h>

#include "renderer/ImageBasedLighting.h"
#include "renderer/atmosphere/SkyAtmosphere.h"
#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/utils/Log.h>
#include <rapture/gpu/textures/Texture.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/systems/Transforms.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <limits>

namespace Rapture {

static constexpr std::string_view KEY_SKY = "sky";
static constexpr std::string_view KEY_TEXTURE = "texture";
static constexpr std::string_view KEY_INTENSITY = "intensity";
static constexpr std::string_view KEY_ENABLED = "enabled";
static constexpr std::string_view KEY_USE_ATMOSPHERE = "useAtmosphere";
static constexpr std::string_view KEY_ATMOSPHERE = "atmosphere";
static constexpr std::string_view KEY_TIME_OF_DAY = "timeOfDay";
static constexpr std::string_view KEY_LATITUDE = "latitude";
static constexpr std::string_view KEY_LONGITUDE = "longitude";
static constexpr std::string_view KEY_RAYLEIGH = "rayleigh";
static constexpr std::string_view KEY_RAYLEIGH_SCALE_HEIGHT = "rayleighScaleHeight";
static constexpr std::string_view KEY_MIE_SCATTERING = "mieScattering";
static constexpr std::string_view KEY_MIE_ABSORPTION = "mieAbsorption";
static constexpr std::string_view KEY_MIE_SCALE_HEIGHT = "mieScaleHeight";
static constexpr std::string_view KEY_MIE_G = "mieG";
static constexpr std::string_view KEY_OZONE_DENSITY = "ozoneDensity";
static constexpr std::string_view KEY_GROUND_ALBEDO = "groundAlbedo";
static constexpr std::string_view KEY_GROUND_RADIUS = "groundRadius";
static constexpr std::string_view KEY_ATMOSPHERE_THICKNESS = "atmosphereThickness";
static constexpr std::string_view KEY_ORIGIN_ALTITUDE = "originAltitude";
static constexpr std::string_view KEY_TINTS_SUN_LIGHT = "tintsSunLight";

// The illuminance the sky is lit with when no light is marked as the atmosphere's sun
static constexpr float DEFAULT_SUN_ILLUMINANCE = 1.0f;

// Earth's rayleigh scattering coefficient at 550 nm, in 1e-6 per metre
static constexpr double RAYLEIGH_AT_550_NM = 13.558;

static glm::quat s_rotationBetween(const glm::vec3 &from, const glm::vec3 &to)
{
    glm::vec3 a = glm::normalize(from);
    glm::vec3 b = glm::normalize(to);
    float d = glm::dot(a, b);
    if (d > 0.999999f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (d < -0.999999f) {
        glm::vec3 axis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), a);
        if (glm::dot(axis, axis) < 1e-6f) {
            axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), a);
        }
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    return glm::angleAxis(std::acos(d), glm::normalize(glm::cross(a, b)));
}

glm::vec3 Environment::sunDirection(float timeOfDay, float latitude, float longitude)
{
    float hourAngle = (timeOfDay - 12.0f) * (glm::pi<float>() / 12.0f);
    float phi = glm::radians(latitude);
    float delta = 0.0f;

    float sinElev =
        glm::clamp(std::sin(phi) * std::sin(delta) + std::cos(phi) * std::cos(delta) * std::cos(hourAngle), -1.0f, 1.0f);
    float elev = std::asin(sinElev);
    float cosElev = std::cos(elev);

    float sinA = 0.0f;
    float cosA = 1.0f;
    if (cosElev > 1e-4f) {
        sinA = -std::cos(delta) * std::sin(hourAngle) / cosElev;
        cosA = (std::sin(delta) - std::sin(phi) * sinElev) / (std::cos(phi) * cosElev);
    }
    float azimuth = std::atan2(sinA, cosA) + glm::radians(longitude);

    return glm::normalize(glm::vec3(cosElev * std::sin(azimuth), sinElev, -cosElev * std::cos(azimuth)));
}

static float s_wrapToPi(float radians)
{
    float wrapped = std::fmod(radians + glm::pi<float>(), glm::two_pi<float>());
    if (wrapped < 0.0f) {
        wrapped += glm::two_pi<float>();
    }
    return wrapped - glm::pi<float>();
}

SunAngles Environment::sunAnglesFromDirection(const glm::vec3 &sunDirection, const AtmosphereSettings &current)
{
    SunAngles angles{current.timeOfDay, current.longitude};

    float phi = glm::radians(current.latitude);
    float cosPhi = std::cos(phi);
    float sinPhi = std::sin(phi);

    float sinElev = glm::clamp(sunDirection.y, -1.0f, 1.0f);
    float cosElev = std::sqrt(std::max(0.0f, 1.0f - sinElev * sinElev));

    // straight overhead carries no azimuth, and at a pole the elevation no longer varies with the hour
    if (cosElev < 1e-4f || std::abs(cosPhi) < 1e-4f) {
        return angles;
    }

    // an elevation this latitude cannot reach clamps to noon or midnight, the nearest direction it can express
    float hourMagnitude = std::acos(glm::clamp(sinElev / cosPhi, -1.0f, 1.0f));
    float azimuth = std::atan2(sunDirection.x, -sunDirection.z);

    // both hour angle signs solve the elevation, each with its own longitude, so keep the one nearest the current time
    float nearest = std::numeric_limits<float>::max();
    for (float sign : {1.0f, -1.0f}) {
        float hourAngle = sign * hourMagnitude;
        float timeOfDay = 12.0f + hourAngle * (12.0f / glm::pi<float>());
        float distance = std::abs(timeOfDay - current.timeOfDay);
        if (distance >= nearest) {
            continue;
        }
        nearest = distance;

        // the arguments are the forward azimuth's sine and cosine, scaled by the cos(elevation) atan2 ignores
        float baseAzimuth = std::atan2(-std::sin(hourAngle), -sinPhi * sinElev / cosPhi);
        angles.timeOfDay = timeOfDay;
        angles.longitude = glm::degrees(s_wrapToPi(azimuth - baseAzimuth));
    }

    return angles;
}

float Environment::rayleighCoefficient(float wavelengthNm)
{
    return static_cast<float>(RAYLEIGH_AT_550_NM * std::pow(550.0 / wavelengthNm, 4.0));
}

float Environment::wavelengthNm(float rayleighCoefficient)
{
    return static_cast<float>(550.0 * std::pow(RAYLEIGH_AT_550_NM / rayleighCoefficient, 0.25));
}

Environment::Environment(Scene &scene, std::string_view name) : SceneObject(scene, name)
{
    m_lastApplied.timeOfDay = -1.0f;
    m_ibl = std::make_unique<ImageBasedLighting>();
    scene.m_environment = this;
}

Environment::~Environment()
{
    if (scene() != nullptr && scene()->m_environment == this) {
        scene()->m_environment = nullptr;
    }
}

const TypeInfo &Environment::staticType()
{
    static const TypeInfo type("Environment", &SceneObject::staticType());
    return type;
}

const TypeInfo &Environment::type() const
{
    return staticType();
}

AssetHandle Environment::skybox() const
{
    if (!m_skyboxTexture) {
        return INVALID_ASSET_HANDLE;
    }
    return m_skyboxTexture.get()->handle();
}

void Environment::setSkybox(AssetHandle _skybox)
{
    if (skybox() == _skybox) {
        return;
    }

    AssetRef ref = AssetManager::getAsset(_skybox);
    if (!ref) {
        RP_CORE_ERROR("skybox texture {} could not be resolved", _skybox);
        return;
    }

    m_skyboxTexture = ref.as<ATexture>();
}

void Environment::update()
{
    DirectionalLight3D *sunLight = nullptr;
    glm::vec3 sunIlluminance = glm::vec3(DEFAULT_SUN_ILLUMINANCE);
    for (auto [entity, light] : scene()->getRegistry().read<DirectionalLightComponent>().with<TransformComponent>()) {
        if (!light.atmosphereSunLight) {
            continue;
        }

        SceneObject *instance = scene()->instanceFor(entity);
        sunLight = instance != nullptr ? instance->as<DirectionalLight3D>() : nullptr;
        sunIlluminance = light.color * light.intensity;
        break;
    }

    bool sunParamsChanged = m_atmosphere.timeOfDay != m_lastApplied.timeOfDay || m_atmosphere.latitude != m_lastApplied.latitude ||
                            m_atmosphere.longitude != m_lastApplied.longitude;

    glm::vec3 sunDir;
    if (sunLight != nullptr) {
        if (sunParamsChanged) {
            sunDir = sunDirection(m_atmosphere.timeOfDay, m_atmosphere.latitude, m_atmosphere.longitude);
            sunLight->setRotation(s_rotationBetween(glm::vec3(0.0f, 0.0f, -1.0f), -sunDir));
        } else {
            sunDir = -transform::forward(sunLight->worldTransform());
            glm::vec3 expectedDir = sunDirection(m_lastApplied.timeOfDay, m_lastApplied.latitude, m_lastApplied.longitude);
            if (glm::distance(sunDir, expectedDir) > 1e-4f) {
                SunAngles angles = sunAnglesFromDirection(sunDir, m_atmosphere);
                m_atmosphere.timeOfDay = angles.timeOfDay;
                m_atmosphere.longitude = angles.longitude;
            }
        }
    } else {
        sunDir = sunDirection(m_atmosphere.timeOfDay, m_atmosphere.latitude, m_atmosphere.longitude);
    }

    m_lastApplied = m_atmosphere;

    if (!m_usesAtmosphereSkybox) {
        if (sunLight != nullptr) {
            sunLight->setAtmosphereTransmittance(glm::vec3(1.0f));
        }
        return;
    }

    if (sunLight != nullptr) {
        glm::vec3 transmittance = m_atmosphere.tintsSunLight ? SkyAtmosphere::transmittanceToSun(m_atmosphere, sunDir) : glm::vec3(1.0f);
        sunLight->setAtmosphereTransmittance(transmittance);
    }

    ensureSkyAtmosphere();
    if (m_skyAtmosphere == nullptr) {
        return;
    }

    if (m_skyAtmosphere->update(m_atmosphere, sunDir, sunIlluminance)) {
        m_ibl->bakeFromCube(m_skyboxTexture.operator->());
    }
}

void Environment::ensureSkyAtmosphere()
{
    if (m_skyAtmosphere != nullptr) {
        return;
    }

    auto skyAtmosphere = std::make_unique<SkyAtmosphere>();
    if (!skyAtmosphere->isValid()) {
        RP_CORE_ERROR("Failed to create the sky atmosphere");
        return;
    }

    m_skyAtmosphere = std::move(skyAtmosphere);
    m_skyboxTexture = m_skyAtmosphere->getEnvironmentCubemap();
}

SkyAtmosphere *Environment::getSkyAtmosphere() const
{
    if (!m_usesAtmosphereSkybox) {
        return nullptr;
    }
    return m_skyAtmosphere.get();
}

void Environment::serialize(WriteNode node) const
{
    SceneObject::serialize(node);

    WriteNode sky = node.addObject(KEY_SKY);
    // a generated skybox has a handle minted this run, the atmosphere settings below rebuild it instead
    AssetHandle texture = skybox();
    if (texture != INVALID_ASSET_HANDLE && !AssetManager::getAssetMetadata(texture).isVirtualAsset()) {
        sky.set(KEY_TEXTURE, texture);
    }
    sky.set(KEY_INTENSITY, static_cast<double>(m_skyIntensity));
    sky.set(KEY_ENABLED, m_skyboxEnabled);
    sky.set(KEY_USE_ATMOSPHERE, m_usesAtmosphereSkybox);

    WriteNode atmosphere = node.addObject(KEY_ATMOSPHERE);
    atmosphere.set(KEY_TIME_OF_DAY, static_cast<double>(m_atmosphere.timeOfDay));
    atmosphere.set(KEY_LATITUDE, static_cast<double>(m_atmosphere.latitude));
    atmosphere.set(KEY_LONGITUDE, static_cast<double>(m_atmosphere.longitude));
    atmosphere.set(KEY_RAYLEIGH_SCALE_HEIGHT, static_cast<double>(m_atmosphere.rayleighScaleHeight));
    atmosphere.set(KEY_MIE_SCATTERING, static_cast<double>(m_atmosphere.mieScattering));
    atmosphere.set(KEY_MIE_ABSORPTION, static_cast<double>(m_atmosphere.mieAbsorption));
    atmosphere.set(KEY_MIE_SCALE_HEIGHT, static_cast<double>(m_atmosphere.mieScaleHeight));
    atmosphere.set(KEY_MIE_G, static_cast<double>(m_atmosphere.mieG));
    atmosphere.set(KEY_OZONE_DENSITY, static_cast<double>(m_atmosphere.ozoneDensity));
    atmosphere.set(KEY_GROUND_ALBEDO, static_cast<double>(m_atmosphere.groundAlbedo));
    atmosphere.set(KEY_GROUND_RADIUS, static_cast<double>(m_atmosphere.groundRadius));
    atmosphere.set(KEY_ATMOSPHERE_THICKNESS, static_cast<double>(m_atmosphere.atmosphereThickness));
    atmosphere.set(KEY_ORIGIN_ALTITUDE, static_cast<double>(m_atmosphere.originAltitude));
    atmosphere.set(KEY_TINTS_SUN_LIGHT, m_atmosphere.tintsSunLight);

    WriteNode rayleigh = atmosphere.addArray(KEY_RAYLEIGH);
    rayleigh.append(static_cast<double>(m_atmosphere.rayleighScattering.x));
    rayleigh.append(static_cast<double>(m_atmosphere.rayleighScattering.y));
    rayleigh.append(static_cast<double>(m_atmosphere.rayleighScattering.z));
}

void Environment::deserialize(ReadNode node)
{
    SceneObject::deserialize(node);

    ReadNode sky = node.child(KEY_SKY);
    if (sky.valid()) {
        m_skyIntensity = static_cast<float>(sky.child(KEY_INTENSITY).asF64(m_skyIntensity));
        m_skyboxEnabled = sky.child(KEY_ENABLED).asBool(m_skyboxEnabled);
        m_usesAtmosphereSkybox = sky.child(KEY_USE_ATMOSPHERE).asBool(m_usesAtmosphereSkybox);

        AssetHandle texture = sky.child(KEY_TEXTURE).asU64(INVALID_ASSET_HANDLE);
        if (!m_usesAtmosphereSkybox && texture != INVALID_ASSET_HANDLE) {
            setSkybox(texture);
        }
    }

    ReadNode atmosphere = node.child(KEY_ATMOSPHERE);
    if (!atmosphere.valid()) {
        return;
    }

    m_atmosphere.timeOfDay = static_cast<float>(atmosphere.child(KEY_TIME_OF_DAY).asF64(m_atmosphere.timeOfDay));
    m_atmosphere.latitude = static_cast<float>(atmosphere.child(KEY_LATITUDE).asF64(m_atmosphere.latitude));
    m_atmosphere.longitude = static_cast<float>(atmosphere.child(KEY_LONGITUDE).asF64(m_atmosphere.longitude));
    m_atmosphere.rayleighScaleHeight =
        static_cast<float>(atmosphere.child(KEY_RAYLEIGH_SCALE_HEIGHT).asF64(m_atmosphere.rayleighScaleHeight));
    m_atmosphere.mieScattering = static_cast<float>(atmosphere.child(KEY_MIE_SCATTERING).asF64(m_atmosphere.mieScattering));
    m_atmosphere.mieAbsorption = static_cast<float>(atmosphere.child(KEY_MIE_ABSORPTION).asF64(m_atmosphere.mieAbsorption));
    m_atmosphere.mieScaleHeight = static_cast<float>(atmosphere.child(KEY_MIE_SCALE_HEIGHT).asF64(m_atmosphere.mieScaleHeight));
    m_atmosphere.mieG = static_cast<float>(atmosphere.child(KEY_MIE_G).asF64(m_atmosphere.mieG));
    m_atmosphere.ozoneDensity = static_cast<float>(atmosphere.child(KEY_OZONE_DENSITY).asF64(m_atmosphere.ozoneDensity));
    m_atmosphere.groundAlbedo = static_cast<float>(atmosphere.child(KEY_GROUND_ALBEDO).asF64(m_atmosphere.groundAlbedo));
    m_atmosphere.groundRadius = static_cast<float>(atmosphere.child(KEY_GROUND_RADIUS).asF64(m_atmosphere.groundRadius));
    m_atmosphere.atmosphereThickness =
        static_cast<float>(atmosphere.child(KEY_ATMOSPHERE_THICKNESS).asF64(m_atmosphere.atmosphereThickness));
    m_atmosphere.originAltitude = static_cast<float>(atmosphere.child(KEY_ORIGIN_ALTITUDE).asF64(m_atmosphere.originAltitude));
    m_atmosphere.tintsSunLight = atmosphere.child(KEY_TINTS_SUN_LIGHT).asBool(m_atmosphere.tintsSunLight);

    ReadNode rayleigh = atmosphere.child(KEY_RAYLEIGH);
    if (rayleigh.size() == 3) {
        m_atmosphere.rayleighScattering = glm::vec3(static_cast<float>(rayleigh.at(0).asF64(m_atmosphere.rayleighScattering.x)),
                                                    static_cast<float>(rayleigh.at(1).asF64(m_atmosphere.rayleighScattering.y)),
                                                    static_cast<float>(rayleigh.at(2).asF64(m_atmosphere.rayleighScattering.z)));
    }
}

} // namespace Rapture
