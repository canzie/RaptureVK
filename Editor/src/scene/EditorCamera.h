#ifndef RAPTURE__EDITOR_CAMERA_H
#define RAPTURE__EDITOR_CAMERA_H

#include <memory>

namespace Rapture {
class Camera3D;
class CameraController;
class Scene;
class SceneObject;
} // namespace Rapture

/**
 * @brief The camera an editor viewport looks through and the controller that flies it, kept out of the scene's tree and tick lists
 */
class EditorCamera {
  public:
    /**
     * @brief Creates the camera and its controller in a scene
     * @param scene The scene the camera looks at, which must outlive this
     */
    explicit EditorCamera(Rapture::Scene &scene);
    ~EditorCamera();

    EditorCamera(const EditorCamera &) = delete;
    EditorCamera &operator=(const EditorCamera &) = delete;

    Rapture::Camera3D &camera() { return *m_camera; }
    Rapture::CameraController &controller() { return *m_controller; }

    /**
     * @brief Moves the camera so a scene object fills the view, keeping the direction it looks along
     * @param object The object to look at, ignored if it has no place in the world
     */
    void focusOnObject(const Rapture::SceneObject &object);

  private:
    std::unique_ptr<Rapture::Camera3D> m_camera;
    std::unique_ptr<Rapture::CameraController> m_controller;
};

#endif // RAPTURE__EDITOR_CAMERA_H
