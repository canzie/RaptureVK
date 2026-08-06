#ifndef RAPTURE__CAMERA_CONTROLLER_H
#define RAPTURE__CAMERA_CONTROLLER_H

#include "input/ControlInput.h"

#include <glm/glm.hpp>

namespace Rapture {

class Camera3D;
struct TransformComponent;

enum class CameraControlMode {
    FLY,  // WASD movement with mouse-look, cursor captured
    ORBIT // orbit / pan / zoom around a focus point
};

/**
 * @brief Drives a free (spectator/editor) camera entity from ControlInput.
 */
class CameraController {
  public:
    /**
     * @brief Construct a controller that possesses a camera.
     * @param camera Camera to drive, which has to outlive the controller.
     */
    explicit CameraController(Camera3D &camera);

    /**
     * @brief Advance the possessed camera from this frame's intent.
     * @param dt Delta time in seconds.
     * @param input Device-agnostic input for this frame.
     */
    void update(float dt, const ControlInput &input);

    /**
     * @brief The camera this controller possesses.
     * @return The possessed camera.
     */
    Camera3D &camera() const { return m_camera; }

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
     * @brief Whether the camera wants the cursor captured this frame.
     * @return True if the cursor should be locked (DISABLED).
     */
    bool desiresCursorCapture() const { return m_desiresCapture; }

    float mouseSensitivity = 0.1f;
    float movementSpeed = 5.0f;
    float orbitSensitivity = 0.3f;
    float panSpeed = 0.0015f;
    float zoomSpeed = 0.15f;
    float maxPitch = 89.0f;

  private:
    void updateFly(float dt, const ControlInput &input, TransformComponent &transform);
    void updateOrbit(const ControlInput &input, TransformComponent &transform);
    void recalcFront();

    Camera3D &m_camera;
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
