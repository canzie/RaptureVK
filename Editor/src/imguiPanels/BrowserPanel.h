#pragma once

#include "Scenes/Entities/Entity.h"


#include <imgui.h>
#include <memory>
#include <string>
#include <vector>



// Forward declaration
class HierarchyNode;

// Hierarchy node to cache entity hierarchy data
class HierarchyNode {
public:
    HierarchyNode(std::shared_ptr<Rapture::Entity> entity, const std::string& name) 
        : entity(entity), entityName(name) {}
    
    std::shared_ptr<Rapture::Entity> entity;
    std::string entityName;
    std::vector<std::shared_ptr<HierarchyNode>> children;
};

class BrowserPanel {
public:
    BrowserPanel();
    ~BrowserPanel();

    void render();

    // Force rebuild of hierarchy cache
    void refreshHierarchyCache() { m_needsHierarchyRebuild = true; }

private:
   // Builds the cached hierarchy from scratch
    void buildHierarchyCache();

    // Recursively renders a row in the hierarchy table
    void renderHierarchyRow(const std::shared_ptr<HierarchyNode>& node, int depth, int& rowIndex);
    
    // Combined list of root nodes for the hierarchy (includes previously independent entities)
    std::vector<std::shared_ptr<HierarchyNode>> m_hierarchyRoots;
    
    // Scene handle for comparison to detect scene changes
    Rapture::Scene* m_cachedScene = nullptr;
    
    // Flag to force hierarchy rebuild
    bool m_needsHierarchyRebuild = true;
    
    // Entity count for scene modification detection
    uint32_t m_lastEntityCount = 0;

    std::weak_ptr<Rapture::Scene> m_scene;

    std::weak_ptr<Rapture::Entity> m_selectedEntity;

    size_t m_sceneActivatedListenerId;
    size_t m_entitySelectedListenerId;
};
