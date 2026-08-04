#ifndef RAPTURE__NODE3D_H
#define RAPTURE__NODE3D_H

#include "scenes/instances/Instance.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture {

/**
 * @brief An instance with a place in the world.
 */
class Node3D : public Instance {
  public:
    Node3D(Scene &scene, std::string_view name);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    glm::vec3 position() const;
    void setPosition(const glm::vec3 &position);

    glm::vec3 rotation() const;
    void setRotation(const glm::vec3 &rotation);

    glm::quat rotationQuat() const;
    void setRotation(const glm::quat &rotation);

    glm::vec3 scale() const;
    void setScale(const glm::vec3 &scale);

    glm::mat4 localTransform() const;

    /**
     * @brief Replaces translation, rotation and scale from a matrix
     * @param transform The local transform to decompose
     */
    void setLocalTransform(const glm::mat4 &transform);

    /**
     * @brief The local transform composed with every Node3D above it
     * @return The world transform, skipping ancestors that have no place in the world
     */
    glm::mat4 worldTransform() const;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;
};

} // namespace Rapture

#endif // RAPTURE__NODE3D_H
