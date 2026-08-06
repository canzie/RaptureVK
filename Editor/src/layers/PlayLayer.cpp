#include "PlayLayer.h"

#include "logging/Log.h"
#include "scenes/Scene.h"
#include "scenes/instances/Camera3D.h"
#include "viewport/Viewport.h"

PlayLayer::PlayLayer(Rapture::Scene &scene, Rapture::Viewport &viewport) : Layer("Play Layer"), m_scene(scene), m_viewport(viewport)
{
}

static Rapture::Camera3D *s_findCamera(const Rapture::Instance &parent)
{
    for (const auto &child : parent.children()) {
        if (auto *camera = child->as<Rapture::Camera3D>()) {
            return camera;
        }

        if (auto *found = s_findCamera(*child)) {
            return found;
        }
    }

    return nullptr;
}

void PlayLayer::onAttach()
{
    m_editorCamera = m_viewport.getCamera();

    Rapture::Camera3D *playCamera = s_findCamera(*m_scene.root());
    if (playCamera == nullptr) {
        RP_WARN("'{}' holds no camera to play from, keeping the editor view", m_scene.getSceneName());
        return;
    }

    m_viewport.setCamera(playCamera->entity());
}

void PlayLayer::onDetach()
{
    m_viewport.setCamera(m_editorCamera);
    m_editorCamera = Rapture::Entity::null();
}

void PlayLayer::onUpdate(float dt)
{
    m_scene.stepPhysics(dt);
}
