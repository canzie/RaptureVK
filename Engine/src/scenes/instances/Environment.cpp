#include "Environment.h"

#include "utils/EnginePaths.h"

#include "asset_manager/AssetImportConfig.h"
#include "asset_manager/AssetManager.h"
#include "components/Components.h"
#include "components/systems/Transforms.h"
#include "generators/textures/ProceduralTextures.h"
#include "logging/Log.h"
#include "renderer/ImageBasedLighting.h"
#include "scenes/Scene.h"
#include "scenes/instances/Node3D.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"
#include "window_context/Application.h"

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
static constexpr std::string_view KEY_MIE = "mie";
static constexpr std::string_view KEY_MIE_G = "mieG";
static constexpr std::string_view KEY_SUN_INTENSITY = "sunIntensity";
static constexpr std::string_view KEY_CAMERA_ALTITUDE = "cameraAltitude";
static constexpr std::string_view KEY_RAYLEIGH = "rayleigh";

static AssetHandle s_atmosphereShaderHandle()
{
    static AssetHandle s_handle = 0;
    if (s_handle == 0) {
        auto shaderDir = EnginePaths::shaderDirectory();
        ShaderImportConfig importConfig;
        importConfig.compileInfo.macros.push_back(ShaderMacro("OUTPUT_CUBEMAP"));
        auto asset = AssetManager::importAsset(shaderDir / "glsl/Generators/Atmosphere.cs.glsl", importConfig);
        auto shader = asset ? asset.get()->getUnderlyingAsset<Shader>() : nullptr;
        if (!shader) {
            RP_CORE_ERROR("Failed to load Atmosphere cubemap shader");
            return 0;
        }
        s_handle = asset.get()->getHandle();
    }
    return s_handle;
}

static AtmospherePushConstants s_buildPushConstants(const AtmosphereSettings &atmo, const glm::vec3 &sunDir)
{
    AtmospherePushConstants pc;
    pc.cameraPos = glm::vec3(0.0f);
    pc.innerRadius = 1.0f;
    pc.sunDirection = sunDir;
    pc.outerRadius = 1.025f;
    pc.cameraDir = glm::vec3(0.0f, 0.0f, -1.0f);
    pc.scaleDepth = 0.25f;
    pc.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    pc.kr = 0.0025f;
    pc.invWavelength = atmo.rayleigh;
    pc.km = atmo.mie;
    pc.eSun = atmo.sunIntensity;
    pc.g = atmo.mieG;
    pc.fovY = 1.5708f;
    pc.cameraAltitude = atmo.cameraAltitude;
    return pc;
}

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

    float sinElev = glm::clamp(std::sin(phi) * std::sin(delta) + std::cos(phi) * std::cos(delta) * std::cos(hourAngle), -1.0f, 1.0f);
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
    return static_cast<float>(13.5 * std::pow(550.0 / wavelengthNm, 4.0));
}

float Environment::wavelengthNm(float rayleighCoefficient)
{
    return static_cast<float>(550.0 * std::pow(13.5 / rayleighCoefficient, 0.25));
}

Environment::Environment(Scene &scene, std::string_view name) : Instance(scene, name)
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
    static const TypeInfo type("Environment", &Instance::staticType());
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
    return m_skyboxTexture.ref().get()->getHandle();
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

    m_skyboxTexture = AssetPtr<Texture>(std::move(ref));
}

void Environment::update()
{
    Node3D *sunNode = nullptr;
    for (auto [entity, light] : scene()->getRegistry().read<DirectionalLightComponent>().with<TransformComponent>()) {
        if (!light.atmosphereSunLight) {
            continue;
        }

        Instance *instance = scene()->instanceFor(entity);
        sunNode = instance != nullptr ? instance->as<Node3D>() : nullptr;
        break;
    }

    bool sunParamsChanged = m_atmosphere.timeOfDay != m_lastApplied.timeOfDay || m_atmosphere.latitude != m_lastApplied.latitude ||
                            m_atmosphere.longitude != m_lastApplied.longitude;

    glm::vec3 sunDir;
    if (sunNode != nullptr) {
        if (sunParamsChanged) {
            sunDir = sunDirection(m_atmosphere.timeOfDay, m_atmosphere.latitude, m_atmosphere.longitude);
            sunNode->setRotation(s_rotationBetween(glm::vec3(0.0f, 0.0f, -1.0f), -sunDir));
        } else {
            sunDir = -transform::forward(sunNode->worldTransform());
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

    bool changed = !(m_atmosphere == m_lastApplied);
    m_lastApplied = m_atmosphere;

    if (!m_usesAtmosphereSkybox) {
        return;
    }

    bool created = ensureSkyboxGenerator();
    if (m_skyboxGenerator == nullptr) {
        return;
    }

    if (changed || created) {
        AtmospherePushConstants pc = s_buildPushConstants(m_atmosphere, sunDir);
        m_skyboxGenerator->setPushConstants(pc);
        m_skyboxGenerator->generate();
        m_ibl->bakeFromCube(m_skyboxTexture.get());
    }
}

bool Environment::ensureSkyboxGenerator()
{
    if (m_skyboxGenerator != nullptr) {
        return false;
    }

    AssetHandle handle = s_atmosphereShaderHandle();
    if (handle == 0) {
        return false;
    }

    ProceduralTextureConfig config;
    config.name = "atmosphere_skybox_" + std::to_string(static_cast<uint64_t>(id()));
    config.cubemap = true;
    config.format = TextureFormat::RGBA16F;
    config.wrap = TextureWrap::ClampToEdge;

    m_skyboxGenerator = std::make_unique<ProceduralTexture>(handle, config);
    if (!m_skyboxGenerator->isValid()) {
        RP_CORE_ERROR("Failed to create atmosphere skybox generator");
        m_skyboxGenerator.reset();
        return false;
    }

    m_skyboxTexture = m_skyboxGenerator->getTextureAsset();
    return true;
}

void Environment::serialize(WriteNode node) const
{
    Instance::serialize(node);

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
    atmosphere.set(KEY_MIE, static_cast<double>(m_atmosphere.mie));
    atmosphere.set(KEY_MIE_G, static_cast<double>(m_atmosphere.mieG));
    atmosphere.set(KEY_SUN_INTENSITY, static_cast<double>(m_atmosphere.sunIntensity));
    atmosphere.set(KEY_CAMERA_ALTITUDE, static_cast<double>(m_atmosphere.cameraAltitude));

    WriteNode rayleigh = atmosphere.addArray(KEY_RAYLEIGH);
    rayleigh.append(static_cast<double>(m_atmosphere.rayleigh.x));
    rayleigh.append(static_cast<double>(m_atmosphere.rayleigh.y));
    rayleigh.append(static_cast<double>(m_atmosphere.rayleigh.z));
}

void Environment::deserialize(ReadNode node)
{
    Instance::deserialize(node);

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
    m_atmosphere.mie = static_cast<float>(atmosphere.child(KEY_MIE).asF64(m_atmosphere.mie));
    m_atmosphere.mieG = static_cast<float>(atmosphere.child(KEY_MIE_G).asF64(m_atmosphere.mieG));
    m_atmosphere.sunIntensity = static_cast<float>(atmosphere.child(KEY_SUN_INTENSITY).asF64(m_atmosphere.sunIntensity));
    m_atmosphere.cameraAltitude = static_cast<float>(atmosphere.child(KEY_CAMERA_ALTITUDE).asF64(m_atmosphere.cameraAltitude));

    ReadNode rayleigh = atmosphere.child(KEY_RAYLEIGH);
    if (rayleigh.size() == 3) {
        m_atmosphere.rayleigh = glm::vec3(static_cast<float>(rayleigh.at(0).asF64(m_atmosphere.rayleigh.x)),
                                          static_cast<float>(rayleigh.at(1).asF64(m_atmosphere.rayleigh.y)),
                                          static_cast<float>(rayleigh.at(2).asF64(m_atmosphere.rayleigh.z)));
    }
}

} // namespace Rapture
