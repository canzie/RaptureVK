#include "BoundingBox.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Rapture {

    BoundingBox::BoundingBox():
        m_min(std::numeric_limits<float>::max()),
        m_max(std::numeric_limits<float>::lowest()),
        m_isValid(false)
    {
    }

    BoundingBox::BoundingBox(const glm::vec3& min, const glm::vec3& max)
    : m_min(min),
      m_max(max),
      m_isValid(true)
    {
    }

    BoundingBox BoundingBox::calculateFromVertices(const std::vector<float> &vertices, size_t stride, size_t offset)
    {
        RP_CORE_ERROR("BoundingBox::calculateFromVertices - Not implemented");
        return BoundingBox();
    }

    BoundingBox BoundingBox::transform(const glm::mat4& matrix) const {
        RAPTURE_PROFILE_FUNCTION();
        
        if (!m_isValid) {
            return BoundingBox(); // Return invalid bounding box
        }
        
        // Transform all 8 corners of the bounding box
        glm::vec3 corners[8];
        corners[0] = glm::vec3(m_min.x, m_min.y, m_min.z);
        corners[1] = glm::vec3(m_min.x, m_min.y, m_max.z);
        corners[2] = glm::vec3(m_min.x, m_max.y, m_min.z);
        corners[3] = glm::vec3(m_min.x, m_max.y, m_max.z);
        corners[4] = glm::vec3(m_max.x, m_min.y, m_min.z);
        corners[5] = glm::vec3(m_max.x, m_min.y, m_max.z);
        corners[6] = glm::vec3(m_max.x, m_max.y, m_min.z);
        corners[7] = glm::vec3(m_max.x, m_max.y, m_max.z);
        
        // Initialize new bounds
        glm::vec3 newMin(std::numeric_limits<float>::max());
        glm::vec3 newMax(std::numeric_limits<float>::lowest());
        
        // Transform each corner and update bounds
        for (int i = 0; i < 8; ++i) {
            glm::vec4 transformedCorner = matrix * glm::vec4(corners[i], 1.0f);
            transformedCorner /= transformedCorner.w; // Perspective division
            
            glm::vec3 transformedVec3(transformedCorner);
            
            newMin = glm::min(newMin, transformedVec3);
            newMax = glm::max(newMax, transformedVec3);
        }
        
        return BoundingBox(newMin, newMax);
    }



    void BoundingBox::logBounds() const {
        if (m_isValid) {
            RP_CORE_INFO("BoundingBox: Min({:.2f}, {:.2f}, {:.2f}), Max({:.2f}, {:.2f}, {:.2f})", 
                m_min.x, m_min.y, m_min.z, m_max.x, m_max.y, m_max.z);
        } else {
            RP_CORE_WARN("BoundingBox: Invalid");
        }
    }

} // namespace Rapture 