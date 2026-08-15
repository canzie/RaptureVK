#include "PlayLayer.h"

#include "input/Input.h"
#include "logging/Log.h"
#include "scenes/instances/controllers/Controller.h"
#include "scenes/World.h"
#include "scenes/instances/Camera3D.h"
#include "viewport/Viewport.h"
#include "window_context/Application.h"

PlayLayer::PlayLayer(Rapture::World &world, Rapture::Viewport &viewport) : Layer("Play Layer"), m_world(world), m_viewport(viewport)
{
}

PlayLayer::~PlayLayer() = default;

static Rapture::ControlInput s_mapPlayerInput(const Rapture::Input &input)
{
    using namespace Rapture;
    ControlInput intent;
    intent.move.x = (input.isKeyPressed(KEY_D) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_A) ? 1.0f : 0.0f);
    intent.move.z = (input.isKeyPressed(KEY_W) ? 1.0f : 0.0f) - (input.isKeyPressed(KEY_S) ? 1.0f : 0.0f);
    intent.look = input.mouseDelta();
    intent.jump = input.isKeyPressed(KEY_SPACE);
    return intent;
}

void PlayLayer::onAttach()
{
    m_input = std::make_unique<Rapture::Input>(&Rapture::Application::getInstance().getWindowContext());
    m_controlReleased = false;

    m_world.play();

    m_editorCamera = m_viewport.getCamera();

    Rapture::Controller *controller = m_world.playController();
    Rapture::Camera3D *playCamera = controller != nullptr ? controller->viewCamera() : nullptr;
    if (playCamera == nullptr) {
        RP_WARN("'{}' holds no camera to play from, keeping the editor view", m_world.getName());
        return;
    }

    m_viewport.setCamera(playCamera->accessor());
}

void PlayLayer::onDetach()
{
    // the cursor is only released each frame by onUpdate, which stops running the moment this returns
    m_input->setCursorMode(Rapture::CursorMode::NORMAL);
    m_input.reset();

    m_viewport.setCamera(m_editorCamera);
    m_editorCamera = Rapture::ecs::EntityAccessor();

    m_world.stop();
}

void PlayLayer::onUpdate(float dt)
{
    (void)dt;

    m_input->onUpdate();

    Rapture::ControlInput mapped = s_mapPlayerInput(*m_input);

    // handed a blank intent rather than none, so the puppet stops where it was instead of drifting
    Rapture::ControlInput intent;
    if (!m_controlReleased) {
        intent = mapped;
    }
    m_world.setIntent(intent);

    Rapture::Controller *controller = m_world.playController();
    bool capture = !m_controlReleased && controller != nullptr && controller->desiresCursorCapture();
    m_input->setCursorMode(capture ? Rapture::CursorMode::DISABLED : Rapture::CursorMode::NORMAL);
}
