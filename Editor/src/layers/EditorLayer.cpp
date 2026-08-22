#include "EditorLayer.h"

#include "scene/instances/controllers/CameraController.h"
#include "input/Input.h"
#include "scene/Scene.h"
#include "scene/instances/Camera3D.h"
#include "renderer/viewport/Viewport.h"
#include "renderer/viewport/ViewportManager.h"
#include "app/Application.h"

#include <glm/glm.hpp>

static constexpr glm::vec3 EDITOR_CAMERA_START = glm::vec3(0.0f, 0.0f, 5.0f);
static constexpr float EDITOR_CAMERA_FOV = 90.0f;
static constexpr float EDITOR_CAMERA_NEAR_PLANE = 0.1f;
static constexpr float EDITOR_CAMERA_FAR_PLANE = 200.0f;

static Rapture::ControlInput s_mapEditorCameraInput(const Rapture::Input &input)
{
    using namespace Rapture;
    ControlInput intent;
    intent.move.x = (input.isKeyPressed(KEY_D) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_A) ? 1.0f : 0.0f);
    intent.move.y = (input.isKeyPressed(KEY_SPACE) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_LEFT_SHIFT) ? 1.0f : 0.0f);
    intent.move.z = (input.isKeyPressed(KEY_W) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_S) ? 1.0f : 0.0f);
    intent.look = input.mouseDelta();
    intent.zoom = input.scrollDelta();
    intent.orbit = input.isMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    intent.pan = intent.orbit && input.isKeyPressed(KEY_LEFT_SHIFT);
    intent.releaseControl = input.isKeyPressed(KEY_ESCAPE);
    return intent;
}

EditorLayer::EditorLayer() : Layer("Editor Layer") {}

EditorLayer::~EditorLayer() = default;

void EditorLayer::onAttach()
{
    auto &app = Rapture::Application::getInstance();
    m_input = std::make_unique<Rapture::Input>(&app.getWindowContext());

    m_viewportCreatedConn = app.getViewportManager().onViewportCreated.connect(
        [this](Rapture::Viewport *viewport) { adoptViewport(viewport); });

    // controls outlive a detach, so whatever drove the scene while this layer was down hands it back
    for (auto &[viewport, control] : m_controls) {
        viewport->getScene()->setActiveController(control.controller.get());
    }
}

void EditorLayer::onDetach()
{
    if (m_input == nullptr) {
        return;
    }

    // the cursor is only released each frame by onUpdate, which stops running the moment this returns
    m_input->setCursorMode(Rapture::CursorMode::NORMAL);
    m_input.reset();
}

void EditorLayer::adoptViewport(Rapture::Viewport *viewport)
{
    // a viewport that came with a camera is looking through someone else's, so it is not ours to drive
    if (viewport->getScene() == nullptr || viewport->getCamera().isValid() || m_controls.count(viewport) != 0) {
        return;
    }

    // owned here and never parented, so it is not part of the scene it looks at and no save or
    // snapshot restore can touch it
    ViewportControl control;
    control.camera = std::make_unique<Rapture::Camera3D>(*viewport->getScene(), "Editor Camera");
    control.camera->setPosition(EDITOR_CAMERA_START);
    control.camera->setFieldOfView(EDITOR_CAMERA_FOV);
    control.camera->setNearPlane(EDITOR_CAMERA_NEAR_PLANE);
    control.camera->setFarPlane(EDITOR_CAMERA_FAR_PLANE);
    viewport->setCamera(control.camera->accessor());
    control.controller = std::make_unique<Rapture::CameraController>(*viewport->getScene(), "Editor Camera Controller");
    // driven from here with this viewport's own intent, so it stays out of the scene's tick lists
    control.controller->setTickEnabled(false);
    control.controller->possess(control.camera.get());

    // terrain streaming and the shadow cascades follow whoever is driving the scene
    viewport->getScene()->setActiveController(control.controller.get());
    viewport->editorBinding().controller = control.controller.get();

    // the camera and controller live in the viewport's scene, which its owner is free to destroy
    // as soon as the viewport is gone, so they cannot outlive it
    control.destroyConn = viewport->onDestroy.connect([this, viewport]() { releaseViewportControl(viewport); });

    m_controls.emplace(viewport, std::move(control));
}

void EditorLayer::releaseViewportControl(Rapture::Viewport *viewport)
{
    auto found = m_controls.find(viewport);
    if (found == m_controls.end()) {
        return;
    }

    Rapture::Scene *scene = viewport->getScene();
    if (scene != nullptr && scene->activeController() == found->second.controller.get()) {
        scene->setActiveController(nullptr);
    }

    m_controls.erase(found);
}

void EditorLayer::onUpdate(float dt)
{
    if (m_input == nullptr) {
        return;
    }

    m_input->onUpdate();

    Rapture::ControlInput mapped = s_mapEditorCameraInput(*m_input);

    bool anyCapture = false;
    for (auto &[viewport, control] : m_controls) {
        bool active = viewport->editorBinding().hovered || control.controller->desiresCursorCapture();
        Rapture::ControlInput intent;
        if (active) {
            intent = mapped;
        }
        control.controller->setIntent(intent);
        control.controller->onUpdate(dt);
        if (control.controller->desiresCursorCapture()) {
            anyCapture = true;
        }
    }

    m_input->setCursorMode(anyCapture ? Rapture::CursorMode::DISABLED : Rapture::CursorMode::NORMAL);
}
