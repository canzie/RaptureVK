#include "EditorLayer.h"

#include "components/Components.h"
#include "components/systems/CameraController.h"
#include "input/Input.h"
#include "scenes/Scene.h"
#include "viewport/Viewport.h"
#include "viewport/ViewportManager.h"
#include "window_context/Application.h"

#include <glm/glm.hpp>

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
}

void EditorLayer::onDetach() {}

void EditorLayer::syncViewportControls()
{
    const auto &viewports = Rapture::Application::getInstance().getViewportManager().getViewports();

    for (const auto &vp : viewports) {
        Rapture::Viewport *viewport = vp.get();
        if (viewport->getScene() == nullptr || m_controls.count(viewport) != 0) {
            continue;
        }

        ViewportControl control;
        control.camera = viewport->getCamera();
        if (!control.camera.isValid()) {
            Rapture::Scene *scene = viewport->getScene();
            control.camera = scene->createEntity("Editor Camera");
            control.camera.addComponent<Rapture::TransformComponent>(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f),
                                                                     glm::vec3(1.0f));
            auto &cameraComp = control.camera.addComponent<Rapture::CameraComponent>(90.0f, 16.0f / 9.0f, 0.1f, 200.0f);
            cameraComp.isMainCamera = true;
            scene->setMainCamera(control.camera);
            viewport->setCamera(control.camera);
        }

        control.controller = std::make_unique<Rapture::CameraController>(control.camera);
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
        } else {
            it = m_controls.erase(it);
        }
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
