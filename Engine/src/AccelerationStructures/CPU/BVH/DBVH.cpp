#include "DBVH.h"
#include "Components/Systems/BoundingBox.h"

namespace Rapture {

static BoundingBox combine(const BoundingBox& a, const BoundingBox& b) {
    glm::vec3 min = glm::min(a.getMin(), b.getMin());
    glm::vec3 max = glm::max(a.getMax(), b.getMax());
    return BoundingBox(min, max);
}

DBVH::DBVH() {
    m_rootNodeId = -1;
    m_nodeCapacity = 16;
    m_nodeCount = 0;
    m_nodes.resize(m_nodeCapacity);

    for (int i = 0; i < m_nodeCapacity - 1; ++i) {
        m_nodes[i].rightChildIndex = i + 1;
        m_nodes[i].height = -1;
    }
    m_nodes[m_nodeCapacity - 1].rightChildIndex = -1;
    m_nodes[m_nodeCapacity - 1].height = -1;
    m_freeList = 0;
}

DBVH::~DBVH() {}

void DBVH::clear() {
    m_rootNodeId = -1;
    m_nodeCount = 0;
    for (int i = 0; i < m_nodeCapacity - 1; ++i) {
        m_nodes[i].rightChildIndex = i + 1;
        m_nodes[i].height = -1;
    }
    m_nodes[m_nodeCapacity - 1].rightChildIndex = -1;
    m_nodes[m_nodeCapacity - 1].height = -1;
    m_freeList = 0;
}

int DBVH::allocateNode() {
    if (m_freeList == -1) {
        int oldCapacity = m_nodeCapacity;
        m_nodeCapacity *= 2;
        m_nodes.resize(m_nodeCapacity);

        for (int i = oldCapacity; i < m_nodeCapacity - 1; ++i) {
            m_nodes[i].rightChildIndex = i + 1;
            m_nodes[i].height = -1;
        }
        m_nodes[m_nodeCapacity - 1].rightChildIndex = -1;
        m_nodes[m_nodeCapacity - 1].height = -1;
        m_freeList = oldCapacity;
    }

    int nodeId = m_freeList;
    m_freeList = m_nodes[nodeId].rightChildIndex;
    m_nodes[nodeId].parentIndex = -1;
    m_nodes[nodeId].leftChildIndex = -1;
    m_nodes[nodeId].rightChildIndex = -1;
    m_nodes[nodeId].height = 0;
    m_nodeCount++;
    return nodeId;
}

void DBVH::freeNode(int nodeId) {
    m_nodes[nodeId].rightChildIndex = m_freeList;
    m_nodes[nodeId].height = -1;
    m_freeList = nodeId;
    m_nodeCount--;
}

int DBVH::insert(EntityID entity, const BoundingBox& aabb) {
    int leafNodeId = allocateNode();
    m_nodes[leafNodeId].min = aabb.getMin();
    m_nodes[leafNodeId].max = aabb.getMax();
    m_nodes[leafNodeId].entityID = entity;

    insertLeaf(leafNodeId);
    return leafNodeId;
}

void DBVH::remove(int nodeId) {
    removeLeaf(nodeId);
    freeNode(nodeId);
}

bool DBVH::update(int nodeId, const BoundingBox& aabb, const glm::vec3& displacement) {
    BoundingBox node_aabb(m_nodes[nodeId].min, m_nodes[nodeId].max);
    if (node_aabb.contains(aabb))
        return false;
    
    removeLeaf(nodeId);
    m_nodes[nodeId].min = aabb.getMin();
    m_nodes[nodeId].max = aabb.getMax();
    insertLeaf(nodeId);

    return true;
}

void DBVH::insertLeaf(int leafNodeId) {
    if (m_rootNodeId == -1) {
        m_rootNodeId = leafNodeId;
        m_nodes[m_rootNodeId].parentIndex = -1;
        return;
    }

    BoundingBox leafAABB(m_nodes[leafNodeId].min, m_nodes[leafNodeId].max);
    int index = m_rootNodeId;
    while (!m_nodes[index].isLeaf()) {
        int leftChild = m_nodes[index].leftChildIndex;
        int rightChild = m_nodes[index].rightChildIndex;

        float area = BoundingBox(m_nodes[index].min, m_nodes[index].max).getSurfaceArea();

        BoundingBox combined(m_nodes[index].min, m_nodes[index].max);
        combined = combine(combined, leafAABB);
        float combinedArea = combined.getSurfaceArea();

        float cost = 2.0f * combinedArea;
        float inheritanceCost = 2.0f * (combinedArea - area);

        auto cost_function = [&](int child_idx) {
            BoundingBox childAABB(m_nodes[child_idx].min, m_nodes[child_idx].max);
            if (m_nodes[child_idx].isLeaf()) {
                 return combine(leafAABB, childAABB).getSurfaceArea() + inheritanceCost;
            } else {
                 BoundingBox combined_child = combine(leafAABB, childAABB);
                 return (combined_child.getSurfaceArea() - childAABB.getSurfaceArea()) + inheritanceCost;
            }
        };

        float costLeft = cost_function(leftChild);
        float costRight = cost_function(rightChild);
        
        if (cost < costLeft && cost < costRight) 
            break;

        if (costLeft < costRight) {
            index = leftChild;
        }
        else {
            index = rightChild;
        }
    }

    int sibling = index;
    int oldParent = m_nodes[sibling].parentIndex;
    int newParent = allocateNode();
    m_nodes[newParent].parentIndex = oldParent;
    
    BoundingBox new_parent_aabb = combine(leafAABB, BoundingBox(m_nodes[sibling].min, m_nodes[sibling].max));
    m_nodes[newParent].min = new_parent_aabb.getMin();
    m_nodes[newParent].max = new_parent_aabb.getMax();
    m_nodes[newParent].height = m_nodes[sibling].height + 1;

    m_nodes[newParent].leftChildIndex = sibling;
    m_nodes[newParent].rightChildIndex = leafNodeId;
    m_nodes[sibling].parentIndex = newParent;
    m_nodes[leafNodeId].parentIndex = newParent;

    if (oldParent != -1) {
        if (m_nodes[oldParent].leftChildIndex == sibling) {
            m_nodes[oldParent].leftChildIndex = newParent;
        }
        else {
            m_nodes[oldParent].rightChildIndex = newParent;
        }
    }
    else {
        m_rootNodeId = newParent;
    }

    index = m_nodes[leafNodeId].parentIndex;
    while (index != -1) {
        balance(index);

        int leftChild = m_nodes[index].leftChildIndex;
        int rightChild = m_nodes[index].rightChildIndex;

        m_nodes[index].height = 1 + std::max(m_nodes[leftChild].height, m_nodes[rightChild].height);
        BoundingBox aabb = combine(BoundingBox(m_nodes[leftChild].min, m_nodes[leftChild].max), BoundingBox(m_nodes[rightChild].min, m_nodes[rightChild].max));
        m_nodes[index].min = aabb.getMin();
        m_nodes[index].max = aabb.getMax();

        index = m_nodes[index].parentIndex;
    }
}

void DBVH::removeLeaf(int leafNodeId) {
    if (leafNodeId == m_rootNodeId) {
        m_rootNodeId = -1;
        return;
    }

    int parentId = m_nodes[leafNodeId].parentIndex;
    int grandParentId = m_nodes[parentId].parentIndex;
    int siblingId = m_nodes[parentId].leftChildIndex == leafNodeId ? m_nodes[parentId].rightChildIndex : m_nodes[parentId].leftChildIndex;

    if (grandParentId != -1) {
        if (m_nodes[grandParentId].leftChildIndex == parentId) {
            m_nodes[grandParentId].leftChildIndex = siblingId;
        } else {
            m_nodes[grandParentId].rightChildIndex = siblingId;
        }
        m_nodes[siblingId].parentIndex = grandParentId;
        freeNode(parentId);

        int index = grandParentId;
        while (index != -1) {
            balance(index);
            index = m_nodes[index].parentIndex;
        }
    } else {
        m_rootNodeId = siblingId;
        m_nodes[siblingId].parentIndex = -1;
        freeNode(parentId);
    }
}

void DBVH::balance(int iA) {
    BVHNode* A = &m_nodes[iA];
    if (A->isLeaf() || A->height < 2) {
        return;
    }

    int iB = A->leftChildIndex;
    int iC = A->rightChildIndex;
    BVHNode* B = &m_nodes[iB];
    BVHNode* C = &m_nodes[iC];

    int balance = C->height - B->height;

    if (balance > 1) {
        int iF = C->leftChildIndex;
        int iG = C->rightChildIndex;
        BVHNode* F = &m_nodes[iF];
        BVHNode* G = &m_nodes[iG];

        C->leftChildIndex = iA;
        C->parentIndex = A->parentIndex;
        A->parentIndex = iC;

        if (C->parentIndex != -1) {
            if (m_nodes[C->parentIndex].leftChildIndex == iA) {
                m_nodes[C->parentIndex].leftChildIndex = iC;
            } else {
                m_nodes[C->parentIndex].rightChildIndex = iC;
            }
        } else {
            m_rootNodeId = iC;
        }

        if (F->height > G->height) {
            C->rightChildIndex = iF;
            A->rightChildIndex = iG;
            G->parentIndex = iA;

            BoundingBox a_aabb = combine(BoundingBox(B->min, B->max), BoundingBox(G->min, G->max));
            A->min = a_aabb.getMin(); A->max = a_aabb.getMax();

            BoundingBox c_aabb = combine(BoundingBox(A->min, A->max), BoundingBox(F->min, F->max));
            C->min = c_aabb.getMin(); C->max = c_aabb.getMax();
            
            A->height = 1 + std::max(B->height, G->height);
            C->height = 1 + std::max(A->height, F->height);
        } else {
            C->rightChildIndex = iG;
            A->rightChildIndex = iF;
            F->parentIndex = iA;

            BoundingBox a_aabb = combine(BoundingBox(B->min, B->max), BoundingBox(F->min, F->max));
            A->min = a_aabb.getMin(); A->max = a_aabb.getMax();
            BoundingBox c_aabb = combine(BoundingBox(A->min, A->max), BoundingBox(G->min, G->max));
            C->min = c_aabb.getMin(); C->max = c_aabb.getMax();

            A->height = 1 + std::max(B->height, F->height);
            C->height = 1 + std::max(A->height, G->height);
        }
    } else if (balance < -1) {
        int iD = B->leftChildIndex;
        int iE = B->rightChildIndex;
        BVHNode* D = &m_nodes[iD];
        BVHNode* E = &m_nodes[iE];

        B->leftChildIndex = iA;
        B->parentIndex = A->parentIndex;
        A->parentIndex = iB;

        if (B->parentIndex != -1) {
            if (m_nodes[B->parentIndex].leftChildIndex == iA) {
                m_nodes[B->parentIndex].leftChildIndex = iB;
            } else {
                m_nodes[B->parentIndex].rightChildIndex = iB;
            }
        } else {
            m_rootNodeId = iB;
        }

        if (D->height > E->height) {
            B->rightChildIndex = iD;
            A->leftChildIndex = iE;
            E->parentIndex = iA;
            
            BoundingBox a_aabb = combine(BoundingBox(C->min, C->max), BoundingBox(E->min, E->max));
            A->min = a_aabb.getMin(); A->max = a_aabb.getMax();
            BoundingBox b_aabb = combine(BoundingBox(A->min, A->max), BoundingBox(D->min, D->max));
            B->min = b_aabb.getMin(); B->max = b_aabb.getMax();

            A->height = 1 + std::max(C->height, E->height);
            B->height = 1 + std::max(A->height, D->height);
        } else {
            B->rightChildIndex = iE;
            A->leftChildIndex = iD;
            D->parentIndex = iA;

            BoundingBox a_aabb = combine(BoundingBox(C->min, C->max), BoundingBox(D->min, D->max));
            A->min = a_aabb.getMin(); A->max = a_aabb.getMax();
            BoundingBox b_aabb = combine(BoundingBox(A->min, A->max), BoundingBox(E->min, E->max));
            B->min = b_aabb.getMin(); B->max = b_aabb.getMax();

            A->height = 1 + std::max(C->height, D->height);
            B->height = 1 + std::max(A->height, E->height);
        }
    }
}

std::vector<EntityID> DBVH::getIntersectingAABBs(const BoundingBox& worldAABB) const {
    std::vector<EntityID> intersectingEntities;
    if (m_rootNodeId == -1) {
        return intersectingEntities;
    }
    getIntersectingAABBsRecursive(worldAABB, m_rootNodeId, intersectingEntities);
    return intersectingEntities;
}

void DBVH::getIntersectingAABBsRecursive(const BoundingBox& worldAABB, int nodeIndex, std::vector<EntityID>& intersectingEntities) const {
    if (nodeIndex == -1) {
        return;
    }

    const auto& node = m_nodes[nodeIndex];
    const auto& world_min = worldAABB.getMin();
    const auto& world_max = worldAABB.getMax();
    const auto& node_min = node.min;
    const auto& node_max = node.max;

    bool intersects = (world_max.x > node_min.x &&
        world_min.x < node_max.x &&
        world_max.y > node_min.y &&
        world_min.y < node_max.y &&
        world_max.z > node_min.z &&
        world_min.z < node_max.z);

    if (!intersects) {
        return;
    }

    if (node.isLeaf()) {
        intersectingEntities.push_back(node.entityID);
        return;
    }

    getIntersectingAABBsRecursive(worldAABB, node.leftChildIndex, intersectingEntities);
    getIntersectingAABBsRecursive(worldAABB, node.rightChildIndex, intersectingEntities);
}


} 