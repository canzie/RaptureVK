#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "Physics/EntropyCommon.h"

// Forward declarations of all collider types
// This is necessary for the base class to have pointers to them.
namespace Rapture::Entropy {

struct SphereCollider;
struct AABBCollider;
struct OBBCollider;
struct CapsuleCollider;
struct CylinderCollider;
struct ConvexHullCollider;

// An enum to give a unique, ordered ID to each collider type.
// This helps in avoiding duplicate intersection code.
enum class ColliderType {
    Sphere,
    AABB,
    OBB,
    Capsule,
    Cylinder,
    ConvexHull
};

// need to create some class that runs when one of these is added to an entity
// it will take the mesh and other data to properly size the collider, or have them completly seperate and in the ui add a button to
// 'fit' a aabb to a mesh for the transform i would like a seperate transformmatrix

// base collider
struct ColliderBase {
    glm::mat4 transform = glm::mat4(1.0f);
    glm::mat4 localTransform = glm::mat4(1.0f);
    bool isVisible = true;

    virtual ~ColliderBase() = default;

    // The first dispatch in the double-dispatch pattern.
    bool intersects(ColliderBase &other, ContactManifold *manifold = nullptr)
    {
        // We use the collider type enum to decide which collider is responsible for the check.
        // This ensures we only have to implement each pair-wise check once.
        if (getColliderType() < other.getColliderType()) return this->dispatch(other, manifold);
        return other.dispatch(*this, manifold);
    }

    // Pure virtual functions to be implemented by derived colliders.
    virtual ColliderType getColliderType() const = 0;
    virtual void getAABB(glm::vec3 &min, glm::vec3 &max) const = 0;
    virtual glm::mat3 calculateInertiaTensor(float mass) const = 0;

    // The second dispatch, which calls the correct overload.
    virtual bool dispatch(ColliderBase &other, ContactManifold *manifold) = 0;
    virtual bool intersect(SphereCollider &other, ContactManifold *manifold) = 0;
    virtual bool intersect(AABBCollider &other, ContactManifold *manifold) = 0;
    virtual bool intersect(OBBCollider &other, ContactManifold *manifold) = 0;
    virtual bool intersect(CapsuleCollider &other, ContactManifold *manifold) = 0;
    virtual bool intersect(CylinderCollider &other, ContactManifold *manifold) = 0;
    virtual bool intersect(ConvexHullCollider &other, ContactManifold *manifold) = 0;
};

struct SphereCollider : public ColliderBase {
    glm::vec3 center;
    float radius;

    ColliderType getColliderType() const override { return ColliderType::Sphere; }
    void getAABB(glm::vec3 &min, glm::vec3 &max) const override;
    glm::mat3 calculateInertiaTensor(float mass) const override;

    bool dispatch(ColliderBase &other, ContactManifold *manifold) override { return other.intersect(*this, manifold); }
    bool intersect(SphereCollider &other, ContactManifold *manifold) override;
    bool intersect(AABBCollider &other, ContactManifold *manifold) override;
    bool intersect(OBBCollider &other, ContactManifold *manifold) override;
    bool intersect(CapsuleCollider &other, ContactManifold *manifold) override;
    bool intersect(CylinderCollider &other, ContactManifold *manifold) override;
    bool intersect(ConvexHullCollider &other, ContactManifold *manifold) override;
};

struct CylinderCollider : public ColliderBase {
    glm::vec3 start;
    glm::vec3 end;
    float radius;

    ColliderType getColliderType() const override { return ColliderType::Cylinder; }
    void getAABB(glm::vec3 &min, glm::vec3 &max) const override;

    bool dispatch(ColliderBase &other, ContactManifold *manifold) override { return other.intersect(*this, manifold); }
    bool intersect(SphereCollider &other, ContactManifold *manifold) override;
    bool intersect(AABBCollider &other, ContactManifold *manifold) override;
    bool intersect(OBBCollider &other, ContactManifold *manifold) override;
    bool intersect(CapsuleCollider &other, ContactManifold *manifold) override;
    bool intersect(CylinderCollider &other, ContactManifold *manifold) override;
    bool intersect(ConvexHullCollider &other, ContactManifold *manifold) override;
};

struct AABBCollider : public ColliderBase {
    glm::vec3 min;
    glm::vec3 max;

    AABBCollider(const glm::vec3 &min, const glm::vec3 &max) : min(min), max(max) {}
    AABBCollider() : min(glm::vec3(0.0f)), max(glm::vec3(0.0f)) {}

    ColliderType getColliderType() const override { return ColliderType::AABB; }
    void getAABB(glm::vec3 &min, glm::vec3 &max) const override;
    glm::mat3 calculateInertiaTensor(float mass) const override;

    bool dispatch(ColliderBase &other, ContactManifold *manifold) override { return other.intersect(*this, manifold); }
    bool intersect(SphereCollider &other, ContactManifold *manifold) override;
    bool intersect(AABBCollider &other, ContactManifold *manifold) override;
    bool intersect(OBBCollider &other, ContactManifold *manifold) override;
    bool intersect(CapsuleCollider &other, ContactManifold *manifold) override;
    bool intersect(CylinderCollider &other, ContactManifold *manifold) override;
    bool intersect(ConvexHullCollider &other, ContactManifold *manifold) override;
};

struct CapsuleCollider : public ColliderBase {
    glm::vec3 start;
    glm::vec3 end;
    float radius;

    ColliderType getColliderType() const override { return ColliderType::Capsule; }
    void getAABB(glm::vec3 &min, glm::vec3 &max) const override;

    bool dispatch(ColliderBase &other, ContactManifold *manifold) override { return other.intersect(*this, manifold); }
    bool intersect(SphereCollider &other, ContactManifold *manifold) override;
    bool intersect(AABBCollider &other, ContactManifold *manifold) override;
    bool intersect(OBBCollider &other, ContactManifold *manifold) override;
    bool intersect(CapsuleCollider &other, ContactManifold *manifold) override;
    bool intersect(CylinderCollider &other, ContactManifold *manifold) override;
    bool intersect(ConvexHullCollider &other, ContactManifold *manifold) override;
};

struct OBBCollider : public ColliderBase {
    glm::vec3 center;
    glm::vec3 extents;
    glm::quat orientation;

    ColliderType getColliderType() const override { return ColliderType::OBB; }
    void getAABB(glm::vec3 &min, glm::vec3 &max) const override;
    glm::mat3 calculateInertiaTensor(float mass) const override;

    bool dispatch(ColliderBase &other, ContactManifold *manifold) override { return other.intersect(*this, manifold); }
    bool intersect(SphereCollider &other, ContactManifold *manifold) override;
    bool intersect(AABBCollider &other, ContactManifold *manifold) override;
    bool intersect(OBBCollider &other, ContactManifold *manifold) override;
    bool intersect(CapsuleCollider &other, ContactManifold *manifold) override;
    bool intersect(CylinderCollider &other, ContactManifold *manifold) override;
    bool intersect(ConvexHullCollider &other, ContactManifold *manifold) override;
};

struct ConvexHullCollider : public ColliderBase {
    std::vector<glm::vec3> vertices;

    ColliderType getColliderType() const override { return ColliderType::ConvexHull; }
    void getAABB(glm::vec3 &min, glm::vec3 &max) const override;

    bool dispatch(ColliderBase &other, ContactManifold *manifold) override { return other.intersect(*this, manifold); }
    bool intersect(SphereCollider &other, ContactManifold *manifold) override;
    bool intersect(AABBCollider &other, ContactManifold *manifold) override;
    bool intersect(OBBCollider &other, ContactManifold *manifold) override;
    bool intersect(CapsuleCollider &other, ContactManifold *manifold) override;
    bool intersect(CylinderCollider &other, ContactManifold *manifold) override;
    bool intersect(ConvexHullCollider &other, ContactManifold *manifold) override;
};

} // namespace Rapture::Entropy
