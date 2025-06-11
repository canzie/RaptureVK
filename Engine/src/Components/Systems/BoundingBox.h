#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Rapture {

class BoundingBox {

public:

    BoundingBox(const glm::vec3& min, const glm::vec3& max);
    BoundingBox();
    ~BoundingBox() = default;

    glm::vec3 getMin() const { return m_min; }
    glm::vec3 getMax() const { return m_max; }
    glm::vec3 getCenter() const { return (m_min + m_max) * 0.5f; }
    glm::vec3 getExtents() const { return m_max - m_min; }
    glm::vec3 getSize() const { return m_max - m_min; }

    bool isValid() const { return m_isValid; }

    void logBounds() const;

    static BoundingBox calculateFromVertices(const std::vector<float>& vertices, size_t stride, size_t offset);

    BoundingBox transform(const glm::mat4& matrix) const;


private:
    glm::vec3 m_min;
    glm::vec3 m_max;
    bool m_isValid;



};



}
