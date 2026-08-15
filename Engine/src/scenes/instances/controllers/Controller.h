#ifndef RAPTURE__CONTROLLER_H
#define RAPTURE__CONTROLLER_H

#include "input/ControlInput.h"
#include "scenes/instances/SceneObject.h"

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

    /**
     * @brief Advances the possessed object from the intent this controller was last handed
     * @param dt Seconds since the last update
     */
    void onUpdate(float dt) override = 0;

    /**
     * @brief Rebuilds the view camera from where it ended up this frame
     */
    virtual void updateViewCamera() = 0;

    /**
     * @brief Whether this controller wants the cursor captured this frame
     */
    virtual bool desiresCursorCapture() const { return false; }

    SceneObject *possessed() const { return m_possessed; }

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
};

} // namespace Rapture

#endif // RAPTURE__CONTROLLER_H
