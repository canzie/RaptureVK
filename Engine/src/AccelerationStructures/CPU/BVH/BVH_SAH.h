#pragma once

#include "AccelerationStructures/CPU/BVH/BVHCommon.h"
#include <memory>
#include <vector>



namespace Rapture {

class Scene;
class BoundingBox;

class BVH_SAH {
public:
    BVH_SAH(LeafType leafType);
    ~BVH_SAH();

    void build(std::shared_ptr<Scene> scene);
    std::vector<EntityID> getIntersectingAABBs(const BoundingBox& worldAABB) const;

private:
    int recursiveBuild(std::vector<BVHNode>& primitives, size_t start, size_t end);
    void getIntersectingAABBsRecursive(const BoundingBox& worldAABB, int nodeIndex, std::vector<EntityID>& intersectingEntities) const;

private:
    std::vector<BVHNode> m_nodes;
    LeafType m_leafType;
};

}
