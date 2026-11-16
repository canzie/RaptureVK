#ifndef RAPTURE__PERSPECTIVECAMERA_H
#define RAPTURE__PERSPECTIVECAMERA_H

#include <glm/glm.hpp>

namespace Rapture {

class PerspectiveCamera {
  public:
    PerspectiveCamera(float fov, float aspect_ratio, float near_plane, float far_plane);
    PerspectiveCamera() = default;
    ~PerspectiveCamera() = default;

    void updateProjectionMatrix(float radians, float ratio, float nearPlane, float farPlane);
    void updateViewMatrix(glm::vec3 translation, glm::vec3 cameraFront);
    void updateViewMatrix(glm::vec3 translation);
    void setViewMatrix(const glm::mat4 &view) { m_viewMatrix = view; }

    inline glm::mat4 getViewMatrix() const { return m_viewMatrix; }
    inline glm::mat4 getProjectionMatrix() const { return m_projectionMatrix; }

  private:
    glm::mat4 m_projectionMatrix;
    glm::mat4 m_viewMatrix;
};

} // namespace Rapture

#endif // RAPTURE__PERSPECTIVECAMERA_H
