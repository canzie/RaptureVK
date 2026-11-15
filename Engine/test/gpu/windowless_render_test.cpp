#include "windowless_application_environment.h"

#include <rapture/assets/asset_manager/ReservedAssets.h>
#include <rapture/gpu/render_targets/SceneRenderTarget.h>
#include <rapture/gpu/textures/Texture.h>
#include <rapture/gpu/vulkan_context/VulkanQueue.h>
#include <rapture/renderer/RenderSettings.h>
#include <rapture/renderer/common.h>
#include <rapture/renderer/viewport/ViewportManager.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/StaticMesh3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

static constexpr uint32_t IMAGE_WIDTH = 128;
static constexpr uint32_t IMAGE_HEIGHT = 128;
static constexpr uint32_t CHANNEL_COUNT = 4;
static constexpr uint32_t FRAMES_TO_RENDER = 4;
static constexpr float FRAME_DELTA_TIME = 1.0f / 60.0f;

static constexpr glm::vec3 SPHERE_CENTRE = glm::vec3(0.0f);
static constexpr float SPHERE_RADIUS = 1.0f;
static constexpr glm::vec3 CAMERA_LOOK_DIRECTION = glm::vec3(0.0f, 0.0f, -1.0f);
static constexpr float CAMERA_FIELD_OF_VIEW = 60.0f;
static constexpr float CAMERA_NEAR_PLANE = 0.1f;
static constexpr float CAMERA_FAR_PLANE = 100.0f;
static constexpr glm::vec3 LIGHT_ROTATION = glm::vec3(-0.6f, 0.5f, 0.0f);
static constexpr float LIGHT_INTENSITY = 3.0f;

static constexpr float MIN_CENTRE_TO_CORNER_DIFFERENCE = 0.01f;

static glm::vec4 s_readHalfFloatPixel(const std::vector<uint8_t> &pixels, uint32_t x, uint32_t y)
{
    size_t offset = (static_cast<size_t>(y) * IMAGE_WIDTH + x) * CHANNEL_COUNT * sizeof(uint16_t);
    uint16_t halves[CHANNEL_COUNT];
    std::memcpy(halves, pixels.data() + offset, sizeof(halves));
    return glm::vec4(glm::unpackHalf1x16(halves[0]), glm::unpackHalf1x16(halves[1]), glm::unpackHalf1x16(halves[2]),
                     glm::unpackHalf1x16(halves[3]));
}

static void s_writeHalfFloatImageAsPng(const std::vector<uint8_t> &pixels, const std::filesystem::path &path)
{
    std::vector<uint8_t> bytes(static_cast<size_t>(IMAGE_WIDTH) * IMAGE_HEIGHT * CHANNEL_COUNT);
    for (uint32_t y = 0; y < IMAGE_HEIGHT; y++) {
        for (uint32_t x = 0; x < IMAGE_WIDTH; x++) {
            glm::vec4 color = glm::clamp(s_readHalfFloatPixel(pixels, x, y), 0.0f, 1.0f);
            size_t offset = (static_cast<size_t>(y) * IMAGE_WIDTH + x) * CHANNEL_COUNT;
            for (uint32_t channel = 0; channel < CHANNEL_COUNT; channel++) {
                bytes[offset + channel] = static_cast<uint8_t>(std::lround(color[static_cast<glm::length_t>(channel)] * 255.0f));
            }
        }
    }

    std::filesystem::create_directories(path.parent_path());
    stbi_write_png(path.string().c_str(), static_cast<int>(IMAGE_WIDTH), static_cast<int>(IMAGE_HEIGHT),
                   static_cast<int>(CHANNEL_COUNT), bytes.data(), static_cast<int>(IMAGE_WIDTH * CHANNEL_COUNT));
}

class WindowlessRender : public WindowlessGpuTest {};

TEST_F(WindowlessRender, SphereStandsOutFromTheBackground)
{
    Rapture::Application &app = Rapture::Application::getInstance();
    ASSERT_TRUE(app.isWindowless());

    Rapture::Scene scene("WindowlessRender");
    scene.active = true;

    auto *sphere = scene.root()->add<Rapture::StaticMesh3D>("Sphere");
    sphere->setMesh(Rapture::RE_PRIMITIVE_SPHERE_MESH);
    sphere->setMaterial(0, Rapture::RE_DEFAULT_MATERIAL_INSTANCE);

    auto *camera = scene.root()->add<Rapture::Camera3D>("Camera");
    camera->setFieldOfView(CAMERA_FIELD_OF_VIEW);
    camera->setNearPlane(CAMERA_NEAR_PLANE);
    camera->setFarPlane(CAMERA_FAR_PLANE);

    Rapture::CameraController controller(scene, "Camera Controller");
    controller.setTickEnabled(false);
    controller.possess(camera);
    controller.focusOn(SPHERE_CENTRE, SPHERE_RADIUS, CAMERA_LOOK_DIRECTION);

    auto *light = scene.root()->add<Rapture::DirectionalLight3D>("Light");
    light->setRotation(LIGHT_ROTATION);
    light->setColor(glm::vec3(1.0f));
    light->setIntensity(LIGHT_INTENSITY);

    Rapture::ViewportContext viewport = app.getViewportManager().createViewport({
        .scene = &scene,
        .camera = camera->accessor(),
        .name = "WindowlessRender",
        .targetType = Rapture::SceneRenderTarget::TargetType::OFFSCREEN,
        .width = IMAGE_WIDTH,
        .height = IMAGE_HEIGHT,
        .framesInFlight = app.getFramesInFlight(),
        .allowReadback = true,
        .enableAccelerationStructures = false,
    });
    ASSERT_NE(viewport.viewport, nullptr);
    viewport.viewport->createRenderer(Rapture::RendererType::DEFERRED);
    viewport.viewport->renderSettings().setFlag(Rapture::RENDER_USE_GLOBAL_ILLUMINATION, false);

    for (uint32_t frame = 0; frame < FRAMES_TO_RENDER; frame++) {
        controller.onUpdate(FRAME_DELTA_TIME);
        scene.onUpdate(FRAME_DELTA_TIME);
        app.runFrame();
    }
    app.getVulkanContext().getGraphicsQueue()->flush();

    uint32_t lastFrame = viewport.viewport->getLastRenderedFrameIndex();
    ASSERT_NE(lastFrame, UINT32_MAX);

    std::vector<uint8_t> pixels = viewport.viewport->getSceneRenderTarget()->getTexture(lastFrame)->readbackData();
    ASSERT_EQ(pixels.size(), static_cast<size_t>(IMAGE_WIDTH) * IMAGE_HEIGHT * CHANNEL_COUNT * sizeof(uint16_t));

    s_writeHalfFloatImageAsPng(pixels, std::filesystem::temp_directory_path() / "rapture_tests" / "windowless_render_sphere.png");

    glm::vec4 centre = s_readHalfFloatPixel(pixels, IMAGE_WIDTH / 2, IMAGE_HEIGHT / 2);
    glm::vec4 corner = s_readHalfFloatPixel(pixels, 0, 0);
    for (glm::length_t channel = 0; channel < 3; channel++) {
        EXPECT_TRUE(std::isfinite(centre[channel]));
        EXPECT_TRUE(std::isfinite(corner[channel]));
    }
    EXPECT_GT(glm::length(glm::vec3(centre) - glm::vec3(corner)), MIN_CENTRE_TO_CORNER_DIFFERENCE);

    app.getViewportManager().destroyViewport(viewport);
}
