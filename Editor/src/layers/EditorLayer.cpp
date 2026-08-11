#include "EditorLayer.h"

#include "modules/controllers/CameraController.h"
#include "input/Input.h"
#include "scenes/Scene.h"
#include "scenes/instances/Camera3D.h"
#include "viewport/Viewport.h"
#include "viewport/ViewportManager.h"
#include "window_context/Application.h"

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
    m_input = std::make_unique<Rapture::Input>(&Rapture::Application::getInstance().getWindowContext());

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

void EditorLayer::syncViewportControls()
{
    const auto &viewports = Rapture::Application::getInstance().getViewportManager().getViewports();

    for (const auto &vp : viewports) {
        Rapture::Viewport *viewport = vp.get();

        // a viewport that came with a camera is looking through someone else's, so it is not ours to drive
        if (viewport->getScene() == nullptr || viewport->getCamera().isValid() || m_controls.count(viewport) != 0) {
            continue;
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
        control.controller = std::make_unique<Rapture::CameraController>();
        control.controller->possess(control.camera.get());

        // terrain streaming and the shadow cascades follow whoever is driving the scene
        viewport->getScene()->setActiveController(control.controller.get());
        viewport->editorBinding().controller = control.controller.get();
        m_controls.emplace(viewport, std::move(control));
    }

    for (auto it = m_controls.begin(); it != m_controls.end();) {
        bool stillExists = false;
        for (const auto &vp : viewports) {
            if (vp.get() == it->first) {
                stillExists = true;
                break;
            }
        }
        if (stillExists) {
            ++it;
            continue;
        }

        // the viewport is already gone, so the scene is reached through the camera rather than through it
        Rapture::Scene *scene = it->second.camera->scene();
        if (scene != nullptr && scene->activeController() == it->second.controller.get()) {
            scene->setActiveController(nullptr);
        }

        it = m_controls.erase(it);
    }
}

void EditorLayer::onUpdate(float dt)
{
    if (m_input == nullptr) {
        return;
    }

    syncViewportControls();

    m_input->onUpdate();

    Rapture::ControlInput mapped = s_mapEditorCameraInput(*m_input);

    bool anyCapture = false;
    for (auto &[viewport, control] : m_controls) {
        bool active = viewport->editorBinding().hovered || control.controller->desiresCursorCapture();
        Rapture::ControlInput intent;
        if (active) {
            intent = mapped;
        }
        control.controller->update(dt, intent);
        if (control.controller->desiresCursorCapture()) {
            anyCapture = true;
        }
    }

    m_input->setCursorMode(anyCapture ? Rapture::CursorMode::DISABLED : Rapture::CursorMode::NORMAL);
}
