#ifndef RAPTURE__EDITOR_LAYER_H
#define RAPTURE__EDITOR_LAYER_H

#include "app/Layer.h"

#include "core/events/EventSignal.h"

#include <memory>
#include <unordered_map>

namespace Rapture {
class Camera3D;
class CameraController;
class Input;
class Viewport;
} // namespace Rapture

/**
 * @brief Owns the editor's view controls: a camera + controller per editor viewport, plus the editor input.
 */
class EditorLayer : public Rapture::Layer {
  public:
    EditorLayer();
    ~EditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(float dt) override;

  private:
    /**
     * @brief One viewport's editor camera, which is not part of the scene it looks at
     */
    struct ViewportControl {
        std::unique_ptr<Rapture::Camera3D> camera;
        std::unique_ptr<Rapture::CameraController> controller;
        Rapture::EventConnection destroyConn;
    };

    /**
     * @brief Gives a viewport a camera to look through and a controller to drive it
     * @param viewport The viewport to take over, ignored if it came with a camera of its own
     */
    void adoptViewport(Rapture::Viewport *viewport);

    /**
     * @brief Drops the control driving a viewport, called as that viewport is destroyed
     * @param viewport The viewport being destroyed
     */
    void releaseViewportControl(Rapture::Viewport *viewport);

    std::unique_ptr<Rapture::Input> m_input;
    std::unordered_map<Rapture::Viewport *, ViewportControl> m_controls;
    Rapture::EventConnection m_viewportCreatedConn;
};

#endif // RAPTURE__EDITOR_LAYER_H
