#pragma once

#include "AccelerationStructures/CPU/BVH/BVHCommon.h"
#include <vector>
#include <memory>


namespace Rapture {

class BoundingBox;
class Scene;


class DBVH {
public:
    DBVH();
    DBVH(std::shared_ptr<Scene> scene);
    ~DBVH();

    int insert(EntityID entity, const BoundingBox& aabb);
    void remove(int nodeId);
    bool update(int nodeId, const BoundingBox& aabb);
    void clear();

    std::vector<EntityID> getIntersectingAABBs(const BoundingBox& worldAABB) const;

    std::vector<BVHNode>& getNodes() { return m_nodes; }

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
