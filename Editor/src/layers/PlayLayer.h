#ifndef RAPTURE__PLAY_LAYER_H
#define RAPTURE__PLAY_LAYER_H

#include "layers/Layer.h"
#include "ecs/entity_accessor.h"

#include <memory>

namespace Rapture {
class Input;
class Viewport;
class World;
} // namespace Rapture

/**
 * @brief Runs a world for as long as it is attached, showing it through the camera it plays from.
 */
class PlayLayer : public Rapture::Layer {
  public:
    /**
     * @brief Creates a layer that plays a world
     * @param world The world to run
     * @param viewport The viewport the world is played in
     */
    PlayLayer(Rapture::World &world, Rapture::Viewport &viewport);

    ~PlayLayer() override;

    void onUpdate(float dt) override;

    /**
     * @brief Hands control of the puppet back after it was released
     */
    void takeControl() { m_controlReleased = false; }

    /**
     * @brief Hands control of the puppet over to the editor, or takes it back
     */
    void toggleControl() { m_controlReleased = !m_controlReleased; }

  protected:
    void onAttach() override;
    void onDetach() override;

  private:
    Rapture::World &m_world;
    Rapture::Viewport &m_viewport;
    Rapture::ecs::EntityAccessor m_editorCamera;
    std::unique_ptr<Rapture::Input> m_input;
    bool m_controlReleased = false;
};

#endif // RAPTURE__PLAY_LAYER_H
