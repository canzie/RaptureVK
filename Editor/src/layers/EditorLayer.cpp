#include "EditorLayer.h"

#include "components/Components.h"
#include "components/systems/CameraController.h"
#include "events/GameEvents.h"
#include "input/Input.h"
#include "scenes/Scene.h"
#include "scenes/SceneManager.h"
#include "viewport/Viewport.h"
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

    m_sceneActivatedListenerId = Rapture::GameEvents::onSceneActivated().addListener(
        [this](std::shared_ptr<Rapture::Scene> scene) { onNewActiveScene(scene); });

    auto activeScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (activeScene != nullptr) {
        onNewActiveScene(activeScene);
    }
}

void EditorLayer::onDetach()
{
    Rapture::GameEvents::onSceneActivated().removeListener(m_sceneActivatedListenerId);
}

void EditorLayer::onNewActiveScene(std::shared_ptr<Rapture::Scene> scene)
{
    if (scene == nullptr) {
        return;
    }

    m_cameraEntity = scene->createEntity("Editor Camera");
    scene->setMainCamera(m_cameraEntity);

    m_cameraEntity.addComponent<Rapture::TransformComponent>(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    auto &camera = m_cameraEntity.addComponent<Rapture::CameraComponent>(90.0f, 16.0f / 9.0f, 0.1f, 200.0f);
    camera.isMainCamera = true;

    m_controller = std::make_unique<Rapture::CameraController>(m_cameraEntity);
    m_registeredOnViewport = false;
}

void EditorLayer::onUpdate(float dt)
{
    if (m_input == nullptr || m_controller == nullptr) {
        return;
    }

    bool hovered = false;
    auto *viewport = Rapture::Application::getInstance().getViewportManager().getPrimaryViewport();
    if (viewport != nullptr) {
        if (!m_registeredOnViewport) {
            viewport->setCamera(m_cameraEntity);
            viewport->editorBinding().controller = m_controller.get();
            m_registeredOnViewport = true;
        }
        hovered = viewport->editorBinding().hovered;
    }

    m_input->onUpdate();

    // Active while hovering, or while an interaction already holds the cursor captured
    bool active = hovered || m_controller->desiresCursorCapture();
    Rapture::ControlInput intent;
    if (active) {
        intent = s_mapEditorCameraInput(*m_input);
    }
    m_controller->update(dt, intent);
    m_input->setCursorMode(m_controller->desiresCursorCapture() ? Rapture::CursorMode::DISABLED : Rapture::CursorMode::NORMAL);
}
