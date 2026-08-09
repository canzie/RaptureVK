#ifndef RAPTURE__PLAYER_CONTROLLER_H
#define RAPTURE__PLAYER_CONTROLLER_H

#include "modules/controllers/Controller.h"

namespace Rapture {

class SpringArm3D;

/**
 * @brief Drives a spawned puppet from ControlInput.
 *
 * Yaw turns the puppet itself and pitch turns the arm its camera hangs from, so how far back and
 * how high the view sits is authored on the puppet rather than set here.
 */
class PlayerController : public Controller {
  public:
    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Takes a spawned puppet and the camera hanging in it
     * @param subject The puppet root to drive, rejected if it has no place in the world
     */
    void possess(Instance *subject) override;

    /**
     * @brief Walks and turns the possessed puppet from this frame's intent
     * @param dt Seconds since the last update
     * @param input Device-agnostic input for this frame
     */
    void update(float dt, const ControlInput &input) override;

    bool desiresCursorCapture() const override { return true; }

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  public:
    float movementSpeed = 5.0f;
    float mouseSensitivity = 0.1f;
    float maxPitch = 89.0f;

  private:
    /**
     * @brief Rebuilds the view matrix from where the camera ended up this frame
     */
    void updateViewCamera();

  private:
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;

    /// The arm the camera hangs from, which pitch turns, null when the puppet has none
    SpringArm3D *m_cameraArm = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__PLAYER_CONTROLLER_H
