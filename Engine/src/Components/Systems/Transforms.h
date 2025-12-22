#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

class Transforms {

  public:
    Transforms();
    Transforms(const glm::mat4 &transform);
    Transforms(const glm::vec3 &translation, const glm::vec3 &rotation, const glm::vec3 &scale);
    Transforms(const glm::vec3 &translation, const glm::quat &rotation, const glm::vec3 &scale);
    ~Transforms();

    glm::mat4 getTransform() const { return m_transform; }
    glm::vec3 getTranslation() const { return m_translation; }
    glm::vec3 getRotation() const { return m_rotationV; }
    glm::quat getRotationQuat() const { return m_rotationQ; }
    glm::vec3 getScale() const { return m_scale; }

    glm::mat4 &getTransform() { return m_transform; }
    glm::vec3 &getTranslation() { return m_translation; }
    glm::vec3 &getRotation() { return m_rotationV; }
    glm::quat &getRotationQuat() { return m_rotationQ; }
    glm::vec3 &getScale() { return m_scale; }

    bool *getDirtyFlag() { return &m_isDirty; }

    void setTransform(const glm::mat4 &transform);
    void setTranslation(const glm::vec3 &translation);
    void setRotation(const glm::vec3 &rotation);
    void setRotation(const glm::quat &rotation);
    void setScale(const glm::vec3 &scale);

    static glm::mat4 recalculateTransform(const glm::vec3 &translation, const glm::vec3 &rotation, const glm::vec3 &scale);
    static void decomposeTransform(const glm::mat4 &transform, glm::vec3 *translation, glm::vec3 *rotation, glm::vec3 *scale);

    // Recalculate the transform matrix
    void recalculateTransform();
    void decomposeTransform();

  private:
    glm::vec3 m_translation;
    glm::vec3 m_rotationV;
    glm::quat m_rotationQ;
    glm::vec3 m_scale;
    glm::mat4 m_transform;

    bool m_isDirty = true;
};
} // namespace Rapture
