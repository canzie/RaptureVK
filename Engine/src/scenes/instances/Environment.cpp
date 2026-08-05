#include "Environment.h"

#include "asset_manager/AssetImportConfig.h"
#include "asset_manager/AssetManager.h"
#include "components/Components.h"
#include "generators/textures/ProceduralTextures.h"
#include "logging/Log.h"
#include "renderer/ImageBasedLighting.h"
#include "scenes/Scene.h"
#include "shaders/Shader.h"
#include "textures/Texture.h"
#include "window_context/Application.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace Rapture {

static AssetHandle s_atmosphereShaderHandle()
{
    static AssetHandle s_handle = 0;
    if (s_handle == 0) {
        auto shaderDir = Application::getInstance().getProject().getProjectShaderDirectory();
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

float Environment::timeOfDayFromSun(const glm::vec3 &sunDirection, float latitude)
{
    float elev = std::asin(glm::clamp(sunDirection.y, -1.0f, 1.0f));
    float cphi = std::cos(glm::radians(latitude));
    float cosH = (std::abs(cphi) > 1e-4f) ? glm::clamp(std::sin(elev) / cphi, -1.0f, 1.0f) : 1.0f;
    float hourAngle = std::acos(cosH);
    if (sunDirection.x > 0.0f) {
        hourAngle = -hourAngle;
    }
    return glm::clamp(12.0f + hourAngle * (12.0f / glm::pi<float>()), 0.0f, 24.0f);
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

void Environment::setSkybox(AssetHandle skybox)
{
    AssetRef ref = AssetManager::getAsset(skybox);
    if (!ref) {
        RP_CORE_ERROR("skybox texture {} could not be resolved", skybox);
        return;
    }

    m_skyboxTexture = AssetPtr<Texture>(std::move(ref));
}

void Environment::update()
{
    TransformComponent *sunTransform = nullptr;
    auto sunView = scene()->getRegistry().view<DirectionalLightComponent, TransformComponent>();
    for (auto handle : sunView) {
        auto [light, transform] = sunView.get<DirectionalLightComponent, TransformComponent>(handle);
        if (light.atmosphereSunLight) {
            sunTransform = &transform;
            break;
        }
    }

    bool sunParamsChanged = m_atmosphere.timeOfDay != m_lastApplied.timeOfDay || m_atmosphere.latitude != m_lastApplied.latitude ||
                            m_atmosphere.longitude != m_lastApplied.longitude;

    glm::vec3 sunDir;
    if (sunTransform != nullptr) {
        if (sunParamsChanged) {
            sunDir = sunDirection(m_atmosphere.timeOfDay, m_atmosphere.latitude, m_atmosphere.longitude);
            sunTransform->transforms.setRotation(s_rotationBetween(glm::vec3(0.0f, 0.0f, -1.0f), -sunDir));
        } else {
            sunDir = -glm::normalize(sunTransform->transforms.getRotationQuat() * glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 expectedDir = sunDirection(m_lastApplied.timeOfDay, m_lastApplied.latitude, m_lastApplied.longitude);
            if (glm::distance(sunDir, expectedDir) > 1e-4f) {
                m_atmosphere.timeOfDay = timeOfDayFromSun(sunDir, m_atmosphere.latitude);
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
    config.name = "atmosphere_skybox";
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

    WriteNode sky = node.addObject("sky");
    sky.set("texture", skybox());
    sky.set("intensity", static_cast<double>(m_skyIntensity));
    sky.set("enabled", m_skyboxEnabled);
    sky.set("useAtmosphere", m_usesAtmosphereSkybox);

    WriteNode atmosphere = node.addObject("atmosphere");
    atmosphere.set("timeOfDay", static_cast<double>(m_atmosphere.timeOfDay));
    atmosphere.set("latitude", static_cast<double>(m_atmosphere.latitude));
    atmosphere.set("longitude", static_cast<double>(m_atmosphere.longitude));
    atmosphere.set("mie", static_cast<double>(m_atmosphere.mie));
    atmosphere.set("mieG", static_cast<double>(m_atmosphere.mieG));
    atmosphere.set("sunIntensity", static_cast<double>(m_atmosphere.sunIntensity));
    atmosphere.set("cameraAltitude", static_cast<double>(m_atmosphere.cameraAltitude));

    WriteNode rayleigh = atmosphere.addArray("rayleigh");
    rayleigh.append(static_cast<double>(m_atmosphere.rayleigh.x));
    rayleigh.append(static_cast<double>(m_atmosphere.rayleigh.y));
    rayleigh.append(static_cast<double>(m_atmosphere.rayleigh.z));
}

void Environment::deserialize(ReadNode node)
{
    Instance::deserialize(node);

    ReadNode sky = node.child("sky");
    if (sky.valid()) {
        AssetHandle texture = sky.child("texture").asU64(INVALID_ASSET_HANDLE);
        if (texture != INVALID_ASSET_HANDLE) {
            setSkybox(texture);
        }
        m_skyIntensity = static_cast<float>(sky.child("intensity").asF64(m_skyIntensity));
        m_skyboxEnabled = sky.child("enabled").asBool(m_skyboxEnabled);
        m_usesAtmosphereSkybox = sky.child("useAtmosphere").asBool(m_usesAtmosphereSkybox);
    }

    ReadNode atmosphere = node.child("atmosphere");
    if (!atmosphere.valid()) {
        return;
    }

    m_atmosphere.timeOfDay = static_cast<float>(atmosphere.child("timeOfDay").asF64(m_atmosphere.timeOfDay));
    m_atmosphere.latitude = static_cast<float>(atmosphere.child("latitude").asF64(m_atmosphere.latitude));
    m_atmosphere.longitude = static_cast<float>(atmosphere.child("longitude").asF64(m_atmosphere.longitude));
    m_atmosphere.mie = static_cast<float>(atmosphere.child("mie").asF64(m_atmosphere.mie));
    m_atmosphere.mieG = static_cast<float>(atmosphere.child("mieG").asF64(m_atmosphere.mieG));
    m_atmosphere.sunIntensity = static_cast<float>(atmosphere.child("sunIntensity").asF64(m_atmosphere.sunIntensity));
    m_atmosphere.cameraAltitude = static_cast<float>(atmosphere.child("cameraAltitude").asF64(m_atmosphere.cameraAltitude));

    ReadNode rayleigh = atmosphere.child("rayleigh");
    if (rayleigh.size() == 3) {
        m_atmosphere.rayleigh = glm::vec3(static_cast<float>(rayleigh.at(0).asF64(m_atmosphere.rayleigh.x)),
                                          static_cast<float>(rayleigh.at(1).asF64(m_atmosphere.rayleigh.y)),
                                          static_cast<float>(rayleigh.at(2).asF64(m_atmosphere.rayleigh.z)));
    }
}

} // namespace Rapture
