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
#include <limits>
#include <unordered_map>

#include "Scenes/Entities/Entity.h"
#include "BVHCommon.h"



namespace Rapture {

class Scene;
class BoundingBox;

class BVH {

public:
    BVH(LeafType leafType);
    ~BVH();


    void build(std::shared_ptr<Scene> scene);

    // given an aabb, return every entity intersecting with it
    std::vector<EntityID> getIntersectingAABBs(const BoundingBox& worldAABB) const;


    std::vector<BVHNode>& getNodes() { return m_nodes; }

private:
    int recursiveBuild(std::vector<BVHNode>& primitives, size_t start, size_t end);
    void getIntersectingAABBsRecursive(const BoundingBox& worldAABB, int nodeIndex, std::vector<EntityID>& intersectingEntities) const;

private:

    std::vector<BVHNode> m_nodes;
    LeafType m_leafType;
};
    
    
}
