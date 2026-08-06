#ifndef RAPTURE__PLAY_LAYER_H
#define RAPTURE__PLAY_LAYER_H

#include "layers/Layer.h"
#include "scenes/entities/Entity.h"

namespace Rapture {
class Scene;
class Viewport;
} // namespace Rapture

/**
 * @brief Runs a scene, driving the simulation and showing it through the camera it plays from.
 */
class PlayLayer : public Rapture::Layer {
  public:
    /**
     * @brief Creates a layer that plays a scene
     * @param scene The scene to run
     * @param viewport The viewport the scene is played in
     */
    PlayLayer(Rapture::Scene &scene, Rapture::Viewport &viewport);

    void onUpdate(float dt) override;

  protected:
    void onAttach() override;
    void onDetach() override;

  private:
    Rapture::Scene &m_scene;
    Rapture::Viewport &m_viewport;
    Rapture::Entity m_editorCamera;
};

#endif // RAPTURE__PLAY_LAYER_H
