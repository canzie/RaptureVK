#include "Frustum.h"
#include "Components/Systems/BoundingBox.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Rapture {
void Frustum::update(const glm::mat4 &projection, const glm::mat4 &view)
{
    RAPTURE_PROFILE_SCOPE("Update Main Camera Frustum");

    // Validate input matrices to prevent crashes
    if (glm::any(glm::isnan(projection[0])) || glm::any(glm::isnan(view[0]))) {
        RP_CORE_ERROR("Received NaN in input matrices, skipping frustum update");
        return;
    }

    // Check for zero matrices which would cause division by zero
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

    // Compute the view-projection matrix
    glm::mat4 viewProj = projection * view;

    // Extract frustum planes from the view-projection matrix
    // Left plane
    _planes[0].x = viewProj[0][3] + viewProj[0][0];
    _planes[0].y = viewProj[1][3] + viewProj[1][0];
    _planes[0].z = viewProj[2][3] + viewProj[2][0];
    _planes[0].w = viewProj[3][3] + viewProj[3][0];

    // Right plane
    _planes[1].x = viewProj[0][3] - viewProj[0][0];
    _planes[1].y = viewProj[1][3] - viewProj[1][0];
    _planes[1].z = viewProj[2][3] - viewProj[2][0];
    _planes[1].w = viewProj[3][3] - viewProj[3][0];

    // Bottom plane
    _planes[2].x = viewProj[0][3] + viewProj[0][1];
    _planes[2].y = viewProj[1][3] + viewProj[1][1];
    _planes[2].z = viewProj[2][3] + viewProj[2][1];
    _planes[2].w = viewProj[3][3] + viewProj[3][1];

    // Top plane
    _planes[3].x = viewProj[0][3] - viewProj[0][1];
    _planes[3].y = viewProj[1][3] - viewProj[1][1];
    _planes[3].z = viewProj[2][3] - viewProj[2][1];
    _planes[3].w = viewProj[3][3] - viewProj[3][1];

    // Near plane
    _planes[4].x = viewProj[0][2];
    _planes[4].y = viewProj[1][2];
    _planes[4].z = viewProj[2][2];
    _planes[4].w = viewProj[3][2];

    // Far plane
    _planes[5].x = viewProj[0][3] - viewProj[0][2];
    _planes[5].y = viewProj[1][3] - viewProj[1][2];
    _planes[5].z = viewProj[2][3] - viewProj[2][2];
    _planes[5].w = viewProj[3][3] - viewProj[3][2];

    // Normalize all planes
    for (auto &plane : _planes) {
        float length = sqrtf(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        if (length > 0.0001f) // Avoid division by near-zero
        {
            plane /= length;
        } else {
            RP_CORE_WARN("Plane normalization failed: near-zero length");
        }
    }
}

FrustumResult Frustum::testBoundingBox(const BoundingBox &boundingBox) const
{
    // Early exit for invalid bounding boxes
    if (!boundingBox.isValid()) {
        return FrustumResult::Outside;
    }

    glm::vec3 center = boundingBox.getCenter();
    glm::vec3 extents = boundingBox.getExtents() * 0.5f; // Use half-extents for radius calculation

    bool intersects = false; // Keep track if it intersects any plane

    // Test against each frustum plane
    for (const auto &plane : _planes) {
        glm::vec3 normal(plane.x, plane.y, plane.z);
        float planeDist = plane.w;

        // Calculate distance from center to plane
        float dist = glm::dot(normal, center) + planeDist;

        // Calculate projected radius of the box onto the plane normal
        float radius = glm::dot(extents, glm::abs(normal));

        // If the center is further away from the plane than the projected radius (negative side),
        // the box is completely outside this plane.
        if (dist < -radius) {
            return FrustumResult::Outside;
        }

        // If the center is within the radius distance from the plane (on either side),
        // the box potentially intersects this plane.
        // We check `dist < radius` which covers both `abs(dist) < radius` cases effectively
        // because if `dist >= radius`, the box is fully on the positive side of this plane.
        if (dist < radius) // Check if the box intersects the plane
        {
            intersects = true;
        }
    }

    // If it didn't intersect any plane (meaning it was fully inside all planes), return Inside.
    // Otherwise, it must be intersecting.
    return intersects ? FrustumResult::Intersect : FrustumResult::Inside;
}
} // namespace Rapture