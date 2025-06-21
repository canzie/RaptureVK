#pragma once


/*
    This BVH class is optimised for the traversal time and quality, the insertion speeds will not be great
    If you need a more dynamic BVH, use the DBVH class
    
    TODO Make it possible to put this bvh on the gpu for better parallel traversal
         IF performance is EVER an issue here we can even build it on the gpu

*/

#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include "Scenes/Entities/Entity.h"

class Scene;
class BoundingBox;




namespace Rapture {

    
enum class LeafType {
    AABB
};

struct BVHNode {
    glm::vec3 min;
    glm::vec3 max;

    EntityID entityID;  // need to make sure to check for validity when returning this entity
    
    uint32_t leftChildIndex;
    uint32_t rightChildIndex;


};


class BVH {

public:
    BVH(LeafType leafType);
    ~BVH();


    void build(std::shared_ptr<Scene> scene);

    // given an aabb, return every entity intersecting with it
    std::vector<Entity*> getIntersectingAABBs(const BoundingBox& worldAABB);


private:

    std::vector<BVHNode> m_nodes;
    LeafType m_leafType;
};
    
    
}
