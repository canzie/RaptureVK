#ifndef RAPTURE__TRANSFORMS_H
#define RAPTURE__TRANSFORMS_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Rapture::transform {

/**
 * @brief Builds a transform matrix from its parts
 * @param translation Position
 * @param rotation Orientation
 * @param scale Scale along each axis
 * @return The composed matrix
 */
glm::mat4 compose(const glm::vec3 &translation, const glm::quat &rotation, const glm::vec3 &scale);

/**
 * @brief Builds a transform matrix from its parts, taking the rotation as euler angles
 * @param translation Position
 * @param eulerRotation Orientation as radians around each axis
 * @param scale Scale along each axis
 * @return The composed matrix
 */
glm::mat4 compose(const glm::vec3 &translation, const glm::vec3 &eulerRotation, const glm::vec3 &scale);

/**
 * @brief Splits a transform matrix into its parts
 * @param matrix The matrix to split
 * @param translation Receives the position
 * @param rotation Receives the orientation
 * @param scale Receives the scale along each axis
 */
void decompose(const glm::mat4 &matrix, glm::vec3 &translation, glm::quat &rotation, glm::vec3 &scale);

/**
 * @brief Reads the position out of a transform matrix
 * @param matrix The matrix to read
 * @return The position
 */
glm::vec3 translation(const glm::mat4 &matrix);

/**
 * @brief Reads the scale out of a transform matrix
 * @param matrix The matrix to read
 * @return The scale along each axis
 */
glm::vec3 scale(const glm::mat4 &matrix);

/**
 * @brief Reads the orientation out of a transform matrix
 * @param matrix The matrix to read
 * @return The orientation
 */
glm::quat rotation(const glm::mat4 &matrix);

/**
 * @brief Reads the orientation out of a transform matrix as euler angles
 * @param matrix The matrix to read
 * @return The orientation as radians around each axis
 */
glm::vec3 eulerRotation(const glm::mat4 &matrix);

/**
 * @brief The direction a transform faces, its negative z axis
 * @param matrix The matrix to read
 * @return The normalized forward direction
 */
glm::vec3 forward(const glm::mat4 &matrix);

/**
 * @brief Rebases a world transform onto a parent, giving the local transform that reproduces it
 * @param parentWorld The parent's world transform
 * @param world The world transform to rebase
 * @return The local transform
 */
glm::mat4 toLocal(const glm::mat4 &parentWorld, const glm::mat4 &world);

} // namespace Rapture::transform

#endif // RAPTURE__TRANSFORMS_H
