#include "ColliderPrimitives.h"
#include <limits>
#include <algorithm>
#include <cassert>
#include <vector>

namespace Rapture::Entropy {

// =====================================================================================================================
// SphereCollider
// =====================================================================================================================

void SphereCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // Calculate world-space center.
    glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(center, 1.0f));

    // Estimate uniform scale by averaging the length of the basis vectors.
    glm::vec3 axisX = glm::vec3(transform[0]);
    glm::vec3 axisY = glm::vec3(transform[1]);
    glm::vec3 axisZ = glm::vec3(transform[2]);

    float scale = (glm::length(axisX) + glm::length(axisY) + glm::length(axisZ)) / 3.0f;
    float worldRadius = radius * scale;

    min = worldCenter - glm::vec3(worldRadius);
    max = worldCenter + glm::vec3(worldRadius);
}

bool SphereCollider::intersect(SphereCollider& other, ContactManifold* manifold) {
    // World-space centers
    glm::vec3 centerA = glm::vec3(this->transform * glm::vec4(center, 1.0f));
    glm::vec3 centerB = glm::vec3(other.transform * glm::vec4(other.center, 1.0f));

    // Uniform scale approximation
    auto uniformScale = [](const glm::mat4& m) {
        glm::vec3 axisX = glm::vec3(m[0]);
        glm::vec3 axisY = glm::vec3(m[1]);
        glm::vec3 axisZ = glm::vec3(m[2]);
        return (glm::length(axisX) + glm::length(axisY) + glm::length(axisZ)) / 3.0f;
    };

    float rA = radius * uniformScale(this->transform);
    float rB = other.radius * uniformScale(other.transform);

    glm::vec3 vec_AB = centerB - centerA;
    float distSq = glm::dot(vec_AB, vec_AB);
    float radiusSum = rA + rB;

    if (distSq > radiusSum * radiusSum)
        return false;

    if (manifold) {
        float dist = sqrt(distSq);
        ContactPoint contact;
        contact.penetrationDepth = radiusSum - dist;
        
        if (dist > 1e-6f) {
            contact.normalOnB = vec_AB / dist;
        } else {
            contact.normalOnB = glm::vec3(0.0f, 1.0f, 0.0f); // Overlapping, use arbitrary normal
        }

        contact.worldPointB = centerB - contact.normalOnB * rB;
        contact.worldPointA = contact.worldPointB - contact.normalOnB * contact.penetrationDepth;
        contact.restitution = 0.0f; // Placeholder
        contact.friction = 0.0f;    // Placeholder
        manifold->contactPoints.push_back(contact);
    }
    
    return true;
}

bool SphereCollider::intersect(AABBCollider& other, ContactManifold* manifold) {
    // Delegated to AABBCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool SphereCollider::intersect(OBBCollider& other, ContactManifold* manifold) {
    // Delegated to OBBCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool SphereCollider::intersect(CapsuleCollider& other, ContactManifold* manifold) {
    // Delegated to CapsuleCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool SphereCollider::intersect(CylinderCollider& other, ContactManifold* manifold) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool SphereCollider::intersect(ConvexHullCollider& other, ContactManifold* manifold) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

// =====================================================================================================================
// AABBCollider
// =====================================================================================================================

void AABBCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    min = this->min;
    max = this->max;
}

bool AABBCollider::intersect(SphereCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());

    // Retrieve world-space sphere data.
    glm::vec3 sphereCenter = glm::vec3(other.transform * glm::vec4(other.center, 1.0f));
    auto uniformScale = [](const glm::mat4& m) {
        glm::vec3 axisX = glm::vec3(m[0]);
        glm::vec3 axisY = glm::vec3(m[1]);
        glm::vec3 axisZ = glm::vec3(m[2]);
        return (glm::length(axisX) + glm::length(axisY) + glm::length(axisZ)) / 3.0f;
    };
    float sphereRadius = other.radius * uniformScale(other.transform);

    // World-space AABB min/max.
    glm::vec3 boxMin, boxMax;
    getAABB(boxMin, boxMax);

    // Closest point on box to sphere center.
    glm::vec3 closest = glm::clamp(sphereCenter, boxMin, boxMax);
    glm::vec3 diff    = sphereCenter - closest;
    float distSq      = glm::dot(diff, diff);

    if (distSq > sphereRadius * sphereRadius)
        return false;

    if (manifold) {
        float dist = sqrt(distSq);
        ContactPoint contact;
        contact.penetrationDepth = sphereRadius - dist;
        if (dist > 1e-6f) {
            contact.normalOnB = -diff / dist; // Normal from box to sphere
        } else {
             // Sphere center is inside AABB. Find separation axis of least penetration.
            glm::vec3 d_min = sphereCenter - boxMin;
            glm::vec3 d_max = boxMax - sphereCenter;

            float min_penetration = std::numeric_limits<float>::max();
            glm::vec3 normal;

            if (d_min.x < min_penetration) { min_penetration = d_min.x; normal = glm::vec3(-1, 0, 0); }
            if (d_max.x < min_penetration) { min_penetration = d_max.x; normal = glm::vec3(1, 0, 0); }
            if (d_min.y < min_penetration) { min_penetration = d_min.y; normal = glm::vec3(0, -1, 0); }
            if (d_max.y < min_penetration) { min_penetration = d_max.y; normal = glm::vec3(0, 1, 0); }
            if (d_min.z < min_penetration) { min_penetration = d_min.z; normal = glm::vec3(0, 0, -1); }
            if (d_max.z < min_penetration) { min_penetration = d_max.z; normal = glm::vec3(0, 0, 1); }
            
            contact.normalOnB = normal;
            contact.penetrationDepth = min_penetration + sphereRadius;
        }

        contact.worldPointB = closest;
        contact.worldPointA = contact.worldPointB - contact.normalOnB * contact.penetrationDepth;
        contact.restitution = 0.0f; // Placeholder
        contact.friction = 0.0f;    // Placeholder
        manifold->contactPoints.push_back(contact);
    }

    return true;
}

bool AABBCollider::intersect(AABBCollider& other, ContactManifold* manifold) {
    // Helper lambda to transform a local AABB by a matrix and output world-space min/max.
    auto computeWorldBounds = [](const glm::mat4& T,
                                 const glm::vec3& lmin,
                                 const glm::vec3& lmax,
                                 glm::vec3& outMin,
                                 glm::vec3& outMax)
    {
        glm::vec3 corners[8] = {
            {lmin.x, lmin.y, lmin.z}, {lmin.x, lmin.y, lmax.z},
            {lmin.x, lmax.y, lmin.z}, {lmin.x, lmax.y, lmax.z},
            {lmax.x, lmin.y, lmin.z}, {lmax.x, lmin.y, lmax.z},
            {lmax.x, lmax.y, lmin.z}, {lmax.x, lmax.y, lmax.z}
        };

        outMin = glm::vec3(std::numeric_limits<float>::max());
        outMax = glm::vec3(std::numeric_limits<float>::lowest());

        for (const auto& c : corners) {
            glm::vec3 w = glm::vec3(T * glm::vec4(c, 1.0f));
            outMin = glm::min(outMin, w);
            outMax = glm::max(outMax, w);
        }
    };

    // Local bounds
    glm::vec3 localMinA, localMaxA;
    this->getAABB(localMinA, localMaxA);
    glm::vec3 localMinB, localMaxB;
    other.getAABB(localMinB, localMaxB);

    // World bounds
    glm::vec3 minA, maxA, minB, maxB;
    computeWorldBounds(this->transform,  localMinA, localMaxA, minA, maxA);
    computeWorldBounds(other.transform, localMinB, localMaxB, minB, maxB);

    // Compute overlap using center/extents method for robustness.
    glm::vec3 centerA = 0.5f * (minA + maxA);
    glm::vec3 centerB = 0.5f * (minB + maxB);

    glm::vec3 halfSizeA = 0.5f * (maxA - minA);
    glm::vec3 halfSizeB = 0.5f * (maxB - minB);

    glm::vec3 delta      = centerB - centerA;
    glm::vec3 absDelta   = glm::abs(delta);
    glm::vec3 overlap    = halfSizeA + halfSizeB - absDelta;

    // Choose the axis with the smallest overlap (minimum translation vector).
    enum Axis { X, Y, Z }; Axis sepAxis = X;
    float penetrationDepth = overlap.x;

    if (overlap.y < penetrationDepth) { penetrationDepth = overlap.y; sepAxis = Y; }
    if (overlap.z < penetrationDepth) { penetrationDepth = overlap.z; sepAxis = Z; }

    // Contact normal points from B towards A (i.e., into A).
    glm::vec3 normalOnB(0.0f);
    switch (sepAxis) {
        case X: normalOnB.x = (delta.x < 0.0f) ?  1.0f : -1.0f; break;
        case Y: normalOnB.y = (delta.y < 0.0f) ?  1.0f : -1.0f; break;
        case Z: normalOnB.z = (delta.z < 0.0f) ?  1.0f : -1.0f; break;
    }

    // Compute the intersection region (overlap volume) in world-space.
    glm::vec3 overlapMin(std::max(minA.x, minB.x), std::max(minA.y, minB.y), std::max(minA.z, minB.z));
    glm::vec3 overlapMax(std::min(maxA.x, maxB.x), std::min(maxA.y, maxB.y), std::min(maxA.z, maxB.z));

    glm::vec3 contactPointB = 0.5f * (overlapMin + overlapMax);

    switch (sepAxis) {
        case X: contactPointB.x = (normalOnB.x > 0.0f) ? maxB.x : minB.x; break;
        case Y: contactPointB.y = (normalOnB.y > 0.0f) ? maxB.y : minB.y; break;
        case Z: contactPointB.z = (normalOnB.z > 0.0f) ? maxB.z : minB.z; break;
    }

    ContactPoint cp;
    cp.penetrationDepth = penetrationDepth;
    cp.normalOnB        = normalOnB;
    cp.worldPointB      = contactPointB;
    cp.worldPointA      = contactPointB - normalOnB * penetrationDepth;
    cp.restitution      = 0.0f; // Placeholder – should come from material pairs
    cp.friction         = 0.0f; // Placeholder – should come from material pairs
    manifold->contactPoints.push_back(cp);
    
    return true;
}

bool AABBCollider::intersect(OBBCollider& other, ContactManifold* manifold) {
    // Delegated to OBBCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool AABBCollider::intersect(CapsuleCollider& other, ContactManifold* manifold) {
    // Delegated to CapsuleCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool AABBCollider::intersect(CylinderCollider& other, ContactManifold* manifold) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool AABBCollider::intersect(ConvexHullCollider& other, ContactManifold* manifold) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

// =====================================================================================================================
// OBBCollider
// =====================================================================================================================

void OBBCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB by transforming the 8 corners of the box and finding the new min/max.
    min = min;
    max = max;
}

bool OBBCollider::intersect(SphereCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement OBB vs Sphere intersection.
    return false;
}

bool OBBCollider::intersect(AABBCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement OBB vs AABB intersection.
    return false;
}

bool OBBCollider::intersect(OBBCollider& other, ContactManifold* manifold) {
    // TODO: Implement OBB vs OBB intersection (e.g., using SAT).
    return false;
}

bool OBBCollider::intersect(CapsuleCollider& other, ContactManifold* manifold) {
    // Delegated to CapsuleCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool OBBCollider::intersect(CylinderCollider& other, ContactManifold* manifold) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool OBBCollider::intersect(ConvexHullCollider& other, ContactManifold* manifold) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

// =====================================================================================================================
// CapsuleCollider
// =====================================================================================================================

void CapsuleCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB. This involves finding the AABBs of the two sphere ends and the cylinder body,
    // and then merging them.
    min = min;
    max = max;
}

bool CapsuleCollider::intersect(SphereCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Capsule vs Sphere intersection.
    return false;
}

bool CapsuleCollider::intersect(AABBCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Capsule vs AABB intersection.
    return false;
}

bool CapsuleCollider::intersect(OBBCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Capsule vs OBB intersection.
    return false;
}

bool CapsuleCollider::intersect(CapsuleCollider& other, ContactManifold* manifold) {
    // TODO: Implement Capsule vs Capsule intersection.
    return false;
}

bool CapsuleCollider::intersect(CylinderCollider& other, ContactManifold* manifold) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

bool CapsuleCollider::intersect(ConvexHullCollider& other, ContactManifold* manifold) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

// =====================================================================================================================
// CylinderCollider
// =====================================================================================================================

void CylinderCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB for the cylinder.
    min = min;
    max = max;
}

bool CylinderCollider::intersect(SphereCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs Sphere intersection.
    return false;
}

bool CylinderCollider::intersect(AABBCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs AABB intersection.
    return false;
}

bool CylinderCollider::intersect(OBBCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs OBB intersection.
    return false;
}

bool CylinderCollider::intersect(CapsuleCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs Capsule intersection.
    return false;
}

bool CylinderCollider::intersect(CylinderCollider& other, ContactManifold* manifold) {
    // TODO: Implement Cylinder vs Cylinder intersection.
    return false;
}

bool CylinderCollider::intersect(ConvexHullCollider& other, ContactManifold* manifold) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this, manifold);
}

// =====================================================================================================================
// ConvexHullCollider
// =====================================================================================================================

void ConvexHullCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB by transforming all vertices and finding the min/max.
    min = min;
    max = max;
}

bool ConvexHullCollider::intersect(SphereCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs Sphere intersection (e.g., using GJK).
    return false;
}

bool ConvexHullCollider::intersect(AABBCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs AABB intersection (e.g., using SAT).
    return false;
}

bool ConvexHullCollider::intersect(OBBCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs OBB intersection (e.g., using SAT).
    return false;
}

bool ConvexHullCollider::intersect(CapsuleCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs Capsule intersection (e.g., using GJK).
    return false;
}

bool ConvexHullCollider::intersect(CylinderCollider& other, ContactManifold* manifold) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs Cylinder intersection.
    return false;
}

bool ConvexHullCollider::intersect(ConvexHullCollider& other, ContactManifold* manifold) {
    // TODO: Implement ConvexHull vs ConvexHull intersection (e.g., using GJK).
    return false;
}

} // namespace Rapture::Entropy
