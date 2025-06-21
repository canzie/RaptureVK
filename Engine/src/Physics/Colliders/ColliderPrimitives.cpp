#include "ColliderPrimitives.h"
#include <cassert>

namespace Rapture::Entropy {

// =====================================================================================================================
// SphereCollider
// =====================================================================================================================

void SphereCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB using the sphere's center, radius, and transform.
    // For an untransformed sphere, min would be center - radius and max would be center + radius.
    // You must apply the transform to this box to get the final AABB.
    min = center - radius;
    max = center + radius;
}

bool SphereCollider::intersect(SphereCollider& other) {
    // TODO: Implement Sphere vs Sphere intersection.
    return false;
}

bool SphereCollider::intersect(AABBCollider& other) {
    // Delegated to AABBCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool SphereCollider::intersect(OBBCollider& other) {
    // Delegated to OBBCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool SphereCollider::intersect(CapsuleCollider& other) {
    // Delegated to CapsuleCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool SphereCollider::intersect(CylinderCollider& other) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool SphereCollider::intersect(ConvexHullCollider& other) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

// =====================================================================================================================
// AABBCollider
// =====================================================================================================================

void AABBCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB by transforming the 8 corners of the box and finding the new min/max.
    // For now, just returning a copy. An untransformed AABB's AABB is itself.
    min = min;
    max = max;
}

bool AABBCollider::intersect(SphereCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement AABB vs Sphere intersection.
    return false;
}

bool AABBCollider::intersect(AABBCollider& other) {
    // TODO: Implement AABB vs AABB intersection.
    return false;
}

bool AABBCollider::intersect(OBBCollider& other) {
    // Delegated to OBBCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool AABBCollider::intersect(CapsuleCollider& other) {
    // Delegated to CapsuleCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool AABBCollider::intersect(CylinderCollider& other) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool AABBCollider::intersect(ConvexHullCollider& other) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

// =====================================================================================================================
// OBBCollider
// =====================================================================================================================

void OBBCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB by transforming the 8 corners of the box and finding the new min/max.
    min = min;
    max = max;
}

bool OBBCollider::intersect(SphereCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement OBB vs Sphere intersection.
    return false;
}

bool OBBCollider::intersect(AABBCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement OBB vs AABB intersection.
    return false;
}

bool OBBCollider::intersect(OBBCollider& other) {
    // TODO: Implement OBB vs OBB intersection (e.g., using SAT).
    return false;
}

bool OBBCollider::intersect(CapsuleCollider& other) {
    // Delegated to CapsuleCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool OBBCollider::intersect(CylinderCollider& other) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool OBBCollider::intersect(ConvexHullCollider& other) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
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

bool CapsuleCollider::intersect(SphereCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Capsule vs Sphere intersection.
    return false;
}

bool CapsuleCollider::intersect(AABBCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Capsule vs AABB intersection.
    return false;
}

bool CapsuleCollider::intersect(OBBCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Capsule vs OBB intersection.
    return false;
}

bool CapsuleCollider::intersect(CapsuleCollider& other) {
    // TODO: Implement Capsule vs Capsule intersection.
    return false;
}

bool CapsuleCollider::intersect(CylinderCollider& other) {
    // Delegated to CylinderCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

bool CapsuleCollider::intersect(ConvexHullCollider& other) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

// =====================================================================================================================
// CylinderCollider
// =====================================================================================================================

void CylinderCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB for the cylinder.
    min = min;
    max = max;
}

bool CylinderCollider::intersect(SphereCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs Sphere intersection.
    return false;
}

bool CylinderCollider::intersect(AABBCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs AABB intersection.
    return false;
}

bool CylinderCollider::intersect(OBBCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs OBB intersection.
    return false;
}

bool CylinderCollider::intersect(CapsuleCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement Cylinder vs Capsule intersection.
    return false;
}

bool CylinderCollider::intersect(CylinderCollider& other) {
    // TODO: Implement Cylinder vs Cylinder intersection.
    return false;
}

bool CylinderCollider::intersect(ConvexHullCollider& other) {
    // Delegated to ConvexHullCollider as its type enum is greater.
    assert(getColliderType() < other.getColliderType());
    return other.intersect(*this);
}

// =====================================================================================================================
// ConvexHullCollider
// =====================================================================================================================

void ConvexHullCollider::getAABB(glm::vec3& min, glm::vec3& max) const {
    // TODO: Calculate world-space AABB by transforming all vertices and finding the min/max.
    min = min;
    max = max;
}

bool ConvexHullCollider::intersect(SphereCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs Sphere intersection (e.g., using GJK).
    return false;
}

bool ConvexHullCollider::intersect(AABBCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs AABB intersection (e.g., using SAT).
    return false;
}

bool ConvexHullCollider::intersect(OBBCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs OBB intersection (e.g., using SAT).
    return false;
}

bool ConvexHullCollider::intersect(CapsuleCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs Capsule intersection (e.g., using GJK).
    return false;
}

bool ConvexHullCollider::intersect(CylinderCollider& other) {
    assert(getColliderType() > other.getColliderType());
    // TODO: Implement ConvexHull vs Cylinder intersection.
    return false;
}

bool ConvexHullCollider::intersect(ConvexHullCollider& other) {
    // TODO: Implement ConvexHull vs ConvexHull intersection (e.g., using GJK).
    return false;
}

} // namespace Rapture::Entropy
