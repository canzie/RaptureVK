#ifndef RAPTURE__CONTROLLER_H
#define RAPTURE__CONTROLLER_H

#include "input/ControlInput.h"
#include "scene/instances/SceneObject.h"

namespace Rapture {

class Camera3D;

/**
 * @brief Base of the scene objects that drive another scene object from per-frame input.
 *
 * Holds the object it drives and the camera it is seen through separately, so a class is free to
 * point both at the same object or to keep the view on a rig above the subject. It has to outlive
 * what it possesses, so it stands in the tree in its own right rather than under its subject.
 */
class Controller : public SceneObject {
  public:
    Controller(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Hands this controller the scene object it drives
     * @param subject The object to drive, which has to outlive the possession
     */
    virtual void possess(SceneObject *subject);

    /**
     * @brief Releases the possessed object, leaving this controller driving nothing
     */
    void unpossess();

    /**
     * @brief Hands this controller the intent its next update drives from
     * @param intent Device-agnostic input for this frame
     */
    void setIntent(const ControlInput &intent) { m_intent = intent; }

    const ControlInput &intent() const { return m_intent; }

    /**
     * @brief Turns where this controller is aiming, to the right
     * @param degrees How far to turn
     */
    void addYawInput(float degrees);

    /**
     * @brief Tilts where this controller is aiming, upwards, held within the pitch limit
     * @param degrees How far to tilt
     */
    void addPitchInput(float degrees);

    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }

    /**
     * @brief Where this controller is aiming
     * @return The aim as an orientation
     */
    glm::quat controlRotation() const;

    /**
     * @brief The aim flattened onto the ground, what walking forward follows
     * @return A unit vector in world space
     */
    glm::vec3 controlForward() const;

    /**
     * @brief The aim flattened onto the ground, turned a quarter to the right
     * @return A unit vector in world space
     */
    glm::vec3 controlRight() const;

    float maxPitch() const { return m_maxPitch; }
    void setMaxPitch(float degrees) { m_maxPitch = degrees; }

    /**
     * @brief Advances the possessed object from the intent this controller was last handed
     * @param dt Seconds since the last update
     */
    void onUpdate(float dt) override;

    /**
     * @brief Rebuilds the view camera from where it ended up this frame
     */
    virtual void updateViewCamera() = 0;

    /**
     * @brief Whether this controller wants the cursor captured this frame
     */
    bool capturesCursor() const { return m_capturesCursor; }

    /**
     * @brief Asks for the cursor to be captured, or handed back
     * @param captures Whether the cursor should be captured
     */
    void setCapturesCursor(bool captures) { m_capturesCursor = captures; }

    SceneObject *possessed() const { return m_possessed; }

    EventSignal<void(SceneObject *)> onPossessionChanged;
    EventSignal<void(float)> onUpdateEvent;

    /**
     * @brief The camera the viewport this controller drives renders through
     * @return The camera, or nullptr if this controller has none
     */
    Camera3D *viewCamera() const { return m_viewCamera; }
    void setViewCamera(Camera3D *camera) { m_viewCamera = camera; }

  protected:
    SceneObject *m_possessed = nullptr;
    Camera3D *m_viewCamera = nullptr;
    ControlInput m_intent;
    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    float m_maxPitch = 89.0f;
    bool m_capturesCursor = false;
};

} // namespace Rapture

#endif // RAPTURE__CONTROLLER_H
