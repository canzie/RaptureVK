#include "Transforms.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Rapture::transform {

glm::mat4 compose(const glm::vec3 &translation, const glm::quat &rotation, const glm::vec3 &scale)
{
    glm::mat4 matrix = glm::translate(glm::mat4(1.0f), translation);
    matrix = matrix * glm::mat4_cast(rotation);
    return glm::scale(matrix, scale);
}

glm::mat4 compose(const glm::vec3 &translation, const glm::vec3 &eulerRotation, const glm::vec3 &scale)
{
    return compose(translation, glm::quat(eulerRotation), scale);
}

void decompose(const glm::mat4 &matrix, glm::vec3 &translation, glm::quat &rotation, glm::vec3 &scale)
{
    translation = glm::vec3(matrix[3]);

    scale.x = glm::length(glm::vec3(matrix[0]));
    scale.y = glm::length(glm::vec3(matrix[1]));
    scale.z = glm::length(glm::vec3(matrix[2]));

    glm::vec3 axisX = glm::vec3(matrix[0]) / scale.x;
    glm::vec3 axisY = glm::vec3(matrix[1]) / scale.y;
    glm::vec3 axisZ = glm::vec3(matrix[2]) / scale.z;
    rotation = glm::quat_cast(glm::mat3(axisX, axisY, axisZ));
}

glm::vec3 translation(const glm::mat4 &matrix)
{
    return glm::vec3(matrix[3]);
}

glm::vec3 scale(const glm::mat4 &matrix)
{
    return glm::vec3(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));
}

glm::quat rotation(const glm::mat4 &matrix)
{
    glm::vec3 axisScale = scale(matrix);
    glm::vec3 axisX = glm::vec3(matrix[0]) / axisScale.x;
    glm::vec3 axisY = glm::vec3(matrix[1]) / axisScale.y;
    glm::vec3 axisZ = glm::vec3(matrix[2]) / axisScale.z;
    return glm::quat_cast(glm::mat3(axisX, axisY, axisZ));
}

glm::vec3 eulerRotation(const glm::mat4 &matrix)
{
    return glm::eulerAngles(rotation(matrix));
}

glm::vec3 forward(const glm::mat4 &matrix)
{
    return glm::normalize(-glm::vec3(matrix[2]));
}

glm::mat4 toLocal(const glm::mat4 &parentWorld, const glm::mat4 &world)
{
    return glm::inverse(parentWorld) * world;
}

} // namespace Rapture::transform
