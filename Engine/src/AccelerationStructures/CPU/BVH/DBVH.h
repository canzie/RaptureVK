#pragma once

#include "AccelerationStructures/CPU/BVH/BVHCommon.h"
#include <vector>


namespace Rapture {

class BoundingBox;


class DBVH {
public:
    DBVH();
    ~DBVH();

    int insert(EntityID entity, const BoundingBox& aabb);
    void remove(int nodeId);
    bool update(int nodeId, const BoundingBox& aabb, const glm::vec3& displacement);
    void clear();

    std::vector<EntityID> getIntersectingAABBs(const BoundingBox& worldAABB) const;

private:
    int allocateNode();
    void freeNode(int nodeId);

    void insertLeaf(int leafNodeId);
    void removeLeaf(int leafNodeId);

    void balance(int nodeId);
    
    void getIntersectingAABBsRecursive(const BoundingBox& worldAABB, int nodeIndex, std::vector<EntityID>& intersectingEntities) const;

private:
    std::vector<BVHNode> m_nodes;
    int m_rootNodeId;
    int m_freeList;
    int m_nodeCount;
    int m_nodeCapacity;
};

}
