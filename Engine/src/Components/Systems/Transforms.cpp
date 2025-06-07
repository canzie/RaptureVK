#include "Transforms.h"


namespace Rapture {

Transforms::Transforms()
    : m_translation(0.0f, 0.0f, 0.0f),
      m_rotationV(0.0f, 0.0f, 0.0f),
      m_rotationQ(1.0f, 0.0f, 0.0f, 0.0f),
      m_scale(1.0f, 1.0f, 1.0f),
      m_transform(1.0f)
{
}

Transforms::Transforms(const glm::mat4& transform)
    : m_transform(transform)
{
    decomposeTransform();
}

Transforms::Transforms(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
    : m_translation(translation),
      m_rotationV(rotation),
      m_rotationQ(glm::quat(rotation)),
      m_scale(scale),
      m_transform(1.0f)
{
    recalculateTransform();
}

Transforms::Transforms(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
    : m_translation(translation),
      m_rotationV(glm::eulerAngles(rotation)),
      m_rotationQ(rotation),
      m_scale(scale),
      m_transform(1.0f)
{
    recalculateTransform();
}

Transforms::~Transforms()
{
}



void Transforms::setTransform(const glm::mat4& transform)
{
    m_transform = transform;
    decomposeTransform();

}   


void Transforms::setTranslation(const glm::vec3& translation)
{
    m_translation = translation;
    recalculateTransform();
}   


void Transforms::setRotation(const glm::vec3& rotation)
{
    m_rotationQ = glm::quat(rotation);
    m_rotationV = rotation;
    recalculateTransform();
}   


void Transforms::setRotation(const glm::quat& rotation)
{
    m_rotationQ = rotation;
    m_rotationV = glm::eulerAngles(m_rotationQ);
    recalculateTransform();
}      


void Transforms::setScale(const glm::vec3& scale)
{
    m_scale = scale;
    recalculateTransform();
}

glm::mat4 Transforms::recalculateTransform(const glm::vec3 &translation, const glm::vec3 &rotation, const glm::vec3 &scale)
{


    glm::mat4 transformMatrix = glm::mat4(1.0f);
    transformMatrix = glm::translate(transformMatrix, translation);
    transformMatrix = transformMatrix * glm::mat4_cast(glm::quat(rotation));
    transformMatrix = glm::scale(transformMatrix, scale);

    return transformMatrix;
}

void Transforms::decomposeTransform(const glm::mat4 &transform, glm::vec3* translation, glm::vec3* rotation, glm::vec3* scale)
{
    // 1. Extract translation directly - always accurate and efficient
    *translation = glm::vec3(transform[3]);
    
    // 2. Extract scale - use column vector lengths
    scale->x = glm::length(glm::vec3(transform[0]));
    scale->y = glm::length(glm::vec3(transform[1]));
    scale->z = glm::length(glm::vec3(transform[2]));
    
    // Extract rotation
    // Remove scale from the matrix
    glm::mat3 rotationMatrix(
        glm::vec3(transform[0]) / scale->x,
        glm::vec3(transform[1]) / scale->y,
        glm::vec3(transform[2]) / scale->z
    );
    
    // 6. Extract quaternion from rotation matrix
    glm::quat rotationQ = glm::quat_cast(glm::mat4(rotationMatrix));
    *rotation = glm::eulerAngles(rotationQ);
}   

void Transforms::recalculateTransform()
{
    
    glm::mat4 transformMatrix = glm::mat4(1.0f);
    transformMatrix = glm::translate(transformMatrix, m_translation);
    transformMatrix = transformMatrix * glm::mat4_cast(m_rotationQ);
    transformMatrix = glm::scale(transformMatrix, m_scale);

    m_transform = transformMatrix;
}


// extract the translation, rotation, and scale from the transform matrix
// this updates the translation, rotation, and scale variables
void Transforms::decomposeTransform()
{
    // 1. Extract translation directly - always accurate and efficient
    m_translation = glm::vec3(m_transform[3]);
    
    // 2. Extract scale - use column vector lengths
    m_scale.x = glm::length(glm::vec3(m_transform[0]));
    m_scale.y = glm::length(glm::vec3(m_transform[1]));
    m_scale.z = glm::length(glm::vec3(m_transform[2]));
    
    // Extract rotation
    // Remove scale from the matrix
    glm::mat3 rotationMatrix(
        glm::vec3(m_transform[0]) / m_scale.x,
        glm::vec3(m_transform[1]) / m_scale.y,
        glm::vec3(m_transform[2]) / m_scale.z
    );
    
    // 6. Extract quaternion from rotation matrix
    m_rotationQ = glm::quat_cast(glm::mat4(rotationMatrix));
    m_rotationV = glm::eulerAngles(m_rotationQ);
}
}





