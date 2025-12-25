#include "Frustum.h"

#include "Buffers/StorageBuffers/StorageBuffer.h"
#include "Components/Systems/BoundingBox.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "WindowContext/Application.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Rapture {

Frustum::~Frustum()
{
    m_gpuBuffer.reset();
}

void Frustum::update(const glm::mat4 &projection, const glm::mat4 &view)
{
    RAPTURE_PROFILE_SCOPE("Update Main Camera Frustum");

    if (glm::any(glm::isnan(projection[0])) || glm::any(glm::isnan(view[0]))) {
        RP_CORE_ERROR("Received NaN in input matrices, skipping frustum update");
        return;
    }

    bool projectionIsZero = true;
    bool viewIsZero = true;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (abs(projection[i][j]) > 0.0001f) projectionIsZero = false;
            if (abs(view[i][j]) > 0.0001f) viewIsZero = false;
        }
    }

    if (projectionIsZero || viewIsZero) {
        RP_CORE_WARN("Received zero matrix (projection: {}, view: {}), skipping frustum update", projectionIsZero, viewIsZero);
        return;
    }

    glm::mat4 viewProj = projection * view;

    // Left plane
    m_planes[0].x = viewProj[0][3] + viewProj[0][0];
    m_planes[0].y = viewProj[1][3] + viewProj[1][0];
    m_planes[0].z = viewProj[2][3] + viewProj[2][0];
    m_planes[0].w = viewProj[3][3] + viewProj[3][0];

    // Right plane
    m_planes[1].x = viewProj[0][3] - viewProj[0][0];
    m_planes[1].y = viewProj[1][3] - viewProj[1][0];
    m_planes[1].z = viewProj[2][3] - viewProj[2][0];
    m_planes[1].w = viewProj[3][3] - viewProj[3][0];

    // Bottom plane
    m_planes[2].x = viewProj[0][3] + viewProj[0][1];
    m_planes[2].y = viewProj[1][3] + viewProj[1][1];
    m_planes[2].z = viewProj[2][3] + viewProj[2][1];
    m_planes[2].w = viewProj[3][3] + viewProj[3][1];

    // Top plane
    m_planes[3].x = viewProj[0][3] - viewProj[0][1];
    m_planes[3].y = viewProj[1][3] - viewProj[1][1];
    m_planes[3].z = viewProj[2][3] - viewProj[2][1];
    m_planes[3].w = viewProj[3][3] - viewProj[3][1];

    // Near plane
    m_planes[4].x = viewProj[0][2];
    m_planes[4].y = viewProj[1][2];
    m_planes[4].z = viewProj[2][2];
    m_planes[4].w = viewProj[3][2];

    // Far plane
    m_planes[5].x = viewProj[0][3] - viewProj[0][2];
    m_planes[5].y = viewProj[1][3] - viewProj[1][2];
    m_planes[5].z = viewProj[2][3] - viewProj[2][2];
    m_planes[5].w = viewProj[3][3] - viewProj[3][2];

    for (auto &plane : m_planes) {
        float length = sqrtf(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        if (length > 0.0001f) {
            plane /= length;
        } else {
            RP_CORE_WARN("Plane normalization failed: near-zero length");
        }
    }

    m_gpuDirty = true;
}

uint32_t Frustum::getBindlessIndex()
{
    if (!m_gpuBuffer) {
        auto &vc = Application::getInstance().getVulkanContext();
        VkDeviceSize bufferSize = 6 * sizeof(glm::vec4);
        m_gpuBuffer = std::make_shared<StorageBuffer>(bufferSize, BufferUsage::DYNAMIC, vc.getVmaAllocator());
        m_bindlessIndex = m_gpuBuffer->getBindlessIndex();
    }

    if (m_gpuDirty) {
        m_gpuBuffer->addData(m_planes.data(), m_planes.size() * sizeof(glm::vec4), 0);
        m_gpuDirty = false;
    }

    return m_bindlessIndex;
}

FrustumResult Frustum::testBoundingBox(const BoundingBox &boundingBox) const
{
    if (!boundingBox.isValid()) {
        return FrustumResult::Outside;
    }

    glm::vec3 center = boundingBox.getCenter();
    glm::vec3 extents = boundingBox.getExtents() * 0.5f;

    bool intersects = false;

    for (const auto &plane : m_planes) {
        glm::vec3 normal(plane.x, plane.y, plane.z);
        float planeDist = plane.w;

        float dist = glm::dot(normal, center) + planeDist;
        float radius = glm::dot(extents, glm::abs(normal));

        if (dist < -radius) {
            return FrustumResult::Outside;
        }

        if (dist < radius) {
            intersects = true;
        }
    }

    return intersects ? FrustumResult::Intersect : FrustumResult::Inside;
}

} // namespace Rapture