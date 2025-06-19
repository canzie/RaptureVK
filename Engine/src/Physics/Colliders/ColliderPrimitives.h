#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>



namespace Rapture {
namespace Entropy {

// need to create some class that runs when one of these is added to an entity
// it will take the mesh and other data to properly size the collider, or have them completly seperate and in the ui add a button to 'fit' a collider to a mesh
// for the transform i would like a seperate transformmatrix

    // base collider
    struct ColliderBase {
        glm::mat4 transform;
        bool isVisible = true;
    };

    struct SphereColliderComponent : public ColliderBase {
        glm::vec3 center;
        float radius;
    };
    
    struct CylinderColliderComponent : public ColliderBase {
        glm::vec3 start;
        glm::vec3 end;
        float radius;
    };

    struct AABBColliderComponent : public ColliderBase {
        glm::vec3 min;
        glm::vec3 max;
    };

    struct CapsuleColliderComponent : public ColliderBase {
        glm::vec3 start;
        glm::vec3 end;
        float radius;
    };

    struct OBBColliderComponent : public ColliderBase {
        glm::vec3 center;
        glm::vec3 extents;
        glm::quat orientation;
    };

    struct ConvexHullColliderComponent : public ColliderBase {
        std::vector<glm::vec3> vertices;
    };
    
    

} // namespace Entropy
} // namespace Rapture
