#ifndef RAPTURE__CAMERA_CONTROLLER_H
#define RAPTURE__CAMERA_CONTROLLER_H

#include "scene/instances/controllers/Controller.h"

#include <glm/glm.hpp>

namespace Rapture {

class Node3D;

enum class CameraControlMode {
    FLY,  // WASD movement with mouse-look, cursor captured
    ORBIT // orbit / pan / zoom around a focus point
};

/**
 * @brief Drives a free (spectator/editor) camera entity from ControlInput.
 */
class CameraController : public Controller {
  public:
    CameraController(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    /**
     * @brief Takes a camera as both the object this drives and the view it is seen through
     * @param subject The camera to drive, rejected if it is not a Camera3D
     */
    void possess(SceneObject *subject) override;

    /**
     * @brief Advance the possessed camera from the intent this controller was last handed
     * @param dt Delta time in seconds.
     */
    void onUpdate(float dt) override;

    void updateViewCamera() override;

    /**
     * @brief Whether the camera wants the cursor captured this frame.
     * @return True if the cursor should be locked (DISABLED).
     */
    bool desiresCursorCapture() const override { return m_desiresCapture; }

    /**
     * @brief Get the active control mode.
     * @return The current mode.
     */
    CameraControlMode getMode() const { return m_mode; }

    /**
     * @brief Switch control mode.
     * @param mode Mode to switch to.
     */
    void setMode(CameraControlMode mode);

    /**
     * @brief Places the camera so a sphere fills the view, and orbits the point it is centred on
     * @param center World position the sphere is centred on
     * @param radius Radius of the sphere to fit in view
     */
    void focusOn(const glm::vec3 &center, float radius);

    /**
     * @brief Places the camera so a sphere fills the view, looking along a given direction
     * @param center World position the sphere is centred on
     * @param radius Radius of the sphere to fit in view
     * @param direction Direction to look along, which need not be normalized
     */
    void focusOn(const glm::vec3 &center, float radius, const glm::vec3 &direction);

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  public:
    float mouseSensitivity = 0.1f;
    float movementSpeed = 5.0f;
    float orbitSensitivity = 0.3f;
    float panSpeed = 0.0015f;
    float zoomSpeed = 0.15f;
    float maxPitch = 89.0f;

  private:
    void updateFly(float dt, Node3D &node);
    void updateOrbit(Node3D &node);
    void recalcFront();

  private:
    CameraControlMode m_mode = CameraControlMode::ORBIT;

    float m_yaw = -90.0f;
    float m_pitch = 0.0f;
    glm::vec3 m_front = {0.0f, 0.0f, -1.0f};

    glm::vec3 m_focusPoint = {0.0f, 0.0f, 0.0f};
    float m_focusDistance = 5.0f;
    bool m_recenterFocus = true;

    bool m_desiresCapture = false;
};

} // namespace Rapture

#endif // RAPTURE__CAMERA_CONTROLLER_H
