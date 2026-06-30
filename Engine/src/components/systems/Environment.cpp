#include "Environment.h"

#include "asset_manager/AssetImportConfig.h"
#include "asset_manager/AssetManager.h"
#include "generators/textures/ProceduralTextures.h"
#include "logging/Log.h"
#include "scenes/Scene.h"
#include "shaders/Shader.h"
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

static AtmospherePushConstants s_buildPushConstants(const AtmosphereComponent &atmo, const glm::vec3 &sunDir)
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

Environment::Environment(Entity environment) : m_entity(environment)
{
    m_lastApplied.timeOfDay = -1.0f;
}

Environment::~Environment() = default;

void Environment::update()
{
    auto *atmo = m_entity.tryGetComponent<AtmosphereComponent>();
    if (atmo == nullptr) {
        return;
    }

    TransformComponent *sunTransform = nullptr;
    auto sunView = m_entity.getScene()->getRegistry().view<DirectionalLightComponent, TransformComponent>();
    for (auto handle : sunView) {
        auto [light, transform] = sunView.get<DirectionalLightComponent, TransformComponent>(handle);
        if (light.atmosphereSunLight) {
            sunTransform = &transform;
            break;
        }
    }

    bool sunParamsChanged = atmo->timeOfDay != m_lastApplied.timeOfDay || atmo->latitude != m_lastApplied.latitude ||
                            atmo->longitude != m_lastApplied.longitude;

    glm::vec3 sunDir;
    if (sunTransform != nullptr) {
        if (sunParamsChanged) {
            sunDir = sunDirection(atmo->timeOfDay, atmo->latitude, atmo->longitude);
            sunTransform->transforms.setRotation(s_rotationBetween(glm::vec3(0.0f, 0.0f, -1.0f), -sunDir));
        } else {
            sunDir = -glm::normalize(sunTransform->transforms.getRotationQuat() * glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 expectedDir = sunDirection(m_lastApplied.timeOfDay, m_lastApplied.latitude, m_lastApplied.longitude);
            if (glm::distance(sunDir, expectedDir) > 1e-4f) {
                atmo->timeOfDay = timeOfDayFromSun(sunDir, atmo->latitude);
            }
        }
    } else {
        sunDir = sunDirection(atmo->timeOfDay, atmo->latitude, atmo->longitude);
    }

    bool changed = !(*atmo == m_lastApplied);
    m_lastApplied = *atmo;

    auto *sky = m_entity.tryGetComponent<SkyboxComponent>();
    if (sky == nullptr || !sky->useAtmosphereSkybox) {
        return;
    }

    bool created = ensureSkyboxGenerator(*sky);
    if (m_skyboxGenerator == nullptr) {
        return;
    }

    if (changed || created) {
        AtmospherePushConstants pc = s_buildPushConstants(*atmo, sunDir);
        m_skyboxGenerator->setPushConstants(pc);
        m_skyboxGenerator->generate();
    }
}

bool Environment::ensureSkyboxGenerator(SkyboxComponent &sky)
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

    sky.skyboxTexture = &m_skyboxGenerator->getTexture();
    return true;
}

} // namespace Rapture
