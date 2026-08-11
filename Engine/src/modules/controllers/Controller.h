#ifndef RAPTURE__CONTROLLER_H
#define RAPTURE__CONTROLLER_H

#include "input/ControlInput.h"
#include "modules/ModuleClass.h"

namespace Rapture {

class Camera3D;
class SceneObject;

/**
 * @brief Base of the modules that drive a scene object from per-frame input.
 *
 * A controller holds the object it drives and the camera it is seen through separately, so a class
 * is free to point both at the same object or to keep the view on a rig above the subject.
 */
class Controller : public ModuleClass {
  public:
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
     * @brief Advances the possessed object from this frame's intent
     * @param dt Seconds since the last update
     * @param input Device-agnostic input for this frame
     */
    virtual void update(float dt, const ControlInput &input) = 0;

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
};

} // namespace Rapture

#endif // RAPTURE__CONTROLLER_H
