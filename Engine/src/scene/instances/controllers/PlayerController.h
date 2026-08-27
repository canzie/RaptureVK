#ifndef RAPTURE__PLAYER_CONTROLLER_H
#define RAPTURE__PLAYER_CONTROLLER_H

#include "scene/instances/controllers/Controller.h"

namespace Rapture {

class CharacterBody3D;
class PhysicsBody3D;
class SpringArm3D;

/**
 * @brief Drives a spawned puppet from ControlInput.
 *
 * Yaw turns the puppet itself and pitch turns the arm its camera hangs from, so how far back and
 * how high the view sits is authored on the puppet rather than set here.
 */
class PlayerController : public Controller {
  public:
    PlayerController(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Takes a spawned puppet and the camera hanging in it
     * @param subject The puppet root to drive, rejected if it has no place in the world
     */
    void possess(SceneObject *subject) override;

    /**
     * @brief Walks and turns the possessed puppet from this frame's intent
     * @param dt Seconds since the last update
     */
    void onUpdate(float dt) override;

    void updateViewCamera() override;


    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  public:
    float movementSpeed = 5.0f;
    float mouseSensitivity = 0.1f;

  private:
    /// The arm the camera hangs from, null when the puppet has none
    SpringArm3D *m_cameraArm = nullptr;

    /// The body the puppet moves on, null when the puppet is walked by writing its transform
    PhysicsBody3D *m_body = nullptr;

    /// The body the puppet jumps on, null when its body cannot jump
    CharacterBody3D *m_characterBody = nullptr;
};

} // namespace Rapture

#endif // RAPTURE__PLAYER_CONTROLLER_H
