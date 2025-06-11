#include "BrowserPanel.h"

#include "Components/Components.h"

#include "imguiPanelStyleLinear.h"

#include "Events/GameEvents.h"
#include "Scenes/SceneManager.h"
#include "Logging/TracyProfiler.h"

BrowserPanel::BrowserPanel() {
    // Register for scene activation events - store the ID for cleanup
    m_sceneActivatedListenerId = Rapture::GameEvents::onSceneActivated().addListener(
        [this](std::shared_ptr<Rapture::Scene> scene) {
            
            m_scene = scene;
    });

    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) {
            m_selectedEntity = entity;
    });

    // Check if a scene is already active when the layer is attached
    // This handles the case where the initial scene is set before this layer's listener is registered
    auto currentActiveScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (currentActiveScene) {
        m_scene = currentActiveScene;
    }
}

BrowserPanel::~BrowserPanel() {

    Rapture::GameEvents::onSceneActivated().removeListener(m_sceneActivatedListenerId);
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
}

void BrowserPanel::render() {
    RAPTURE_PROFILE_FUNCTION();

    ImGui::Begin("Entity Browser");

    if (m_scene.lock()) {
        auto& registry = m_scene.lock()->getRegistry();
        
        auto view = registry.view<Rapture::TagComponent>();
        uint32_t entityCount = static_cast<uint32_t>(view.size());
        
        // Display total entities
        ImGui::Text("Total Entities: %d", entityCount);
        
        // Refresh button on the right
        ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f); // Adjust position as needed
        if (ImGui::Button("Refresh")) { // TODO: Replace with icon button if FontAwesome is integrated
            m_needsHierarchyRebuild = true;
        }
        
        ImGui::Separator();
        
        bool entityCountChanged = (m_lastEntityCount != entityCount);
        
        if (entityCountChanged || m_needsHierarchyRebuild) {

            buildHierarchyCache();
            m_lastEntityCount = entityCount;
            m_needsHierarchyRebuild = false;
        }
        
        // Define table structure
        const int columnCount = 3;
        ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable 
                                   | ImGuiTableFlags_RowBg; // Enable RowBg for alternating colors
        
        if (ImGui::BeginTable("EntityHierarchyTable", columnCount, tableFlags)) {
            // Setup columns
            ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f); // Fixed width for type
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f); // Fixed width for actions
            ImGui::TableHeadersRow();
            
            // Render hierarchy rows
            int rowIndex = 0;
            for (const auto& rootNode : m_hierarchyRoots) {
                if (rootNode && rootNode->entity && rootNode->entity->isValid()) {
                    renderHierarchyRow(rootNode, 0, rowIndex);
                }
            }
            
            ImGui::EndTable();
        }
    } else {
        ImGui::Text("No active scene available");
    }
    
    ImGui::End();
}

void BrowserPanel::buildHierarchyCache() {
    
    m_hierarchyRoots.clear();
    
    if (!m_scene.lock()) {
        return;
    }

    auto scene = m_scene.lock();
    
    auto& registry = scene->getRegistry();
    auto view = registry.view<Rapture::TagComponent>();
    
    // Map to store all nodes temporarily
    std::unordered_map<uint32_t, std::shared_ptr<HierarchyNode>> entityNodeMap;
    // Set to keep track of entities that have been added as children
    std::unordered_set<uint32_t> childrenEntities;
    
    // First pass: Create nodes for all entities
    for (auto entityHandle : view) {
        Rapture::Entity entity(entityHandle, scene.get());
        if (!entity.isValid()) continue;
        
        std::string entityName = entity.getComponent<Rapture::TagComponent>().tag;
        
        std::shared_ptr<Rapture::Entity> entityPtr = std::make_shared<Rapture::Entity>(entity);
        entityNodeMap[entity.getID()] = std::make_shared<HierarchyNode>(entityPtr, entityName);
    }
    
    // Second pass: Build parent-child relationships
    for (auto entityHandle : view) {
        Rapture::Entity entity(entityHandle, scene.get());
        if (!entity.isValid()) continue;
        
        uint32_t entityID = entity.getID();
        if (entity.hasComponent<Rapture::EntityNodeComponent>()) {
            auto& nodeComp = entity.getComponent<Rapture::EntityNodeComponent>();
            if (nodeComp.entity_node && nodeComp.entity_node->getParent()) {
                auto parentNode = nodeComp.entity_node->getParent();
                if (parentNode && parentNode->getEntity()) {
                    uint32_t parentID = parentNode->getEntity()->getID();
                    
                    // Find parent and child in map
                    auto parentNodeIt = entityNodeMap.find(parentID);
                    auto childNodeIt = entityNodeMap.find(entityID);
                    
                    if (parentNodeIt != entityNodeMap.end() && childNodeIt != entityNodeMap.end()) {
                        parentNodeIt->second->children.push_back(childNodeIt->second);
                        childrenEntities.insert(entityID); // Mark this entity as a child
                    }
                }
            }
        }
    }
    
    // Third pass: Add root nodes (entities that are not children of anyone)
    for (auto const& [id, node] : entityNodeMap) {
        if (childrenEntities.find(id) == childrenEntities.end()) {
            m_hierarchyRoots.push_back(node);
        }
    }

}

void BrowserPanel::renderHierarchyRow(const std::shared_ptr<HierarchyNode> &node, int depth, int &rowIndex) {
    if (!node || !node->entity || !node->entity->isValid()) {
        return;
    }
    
    ImGui::TableNextRow();
    rowIndex++; // Increment row index for striping
    
    // --- Name Column ---    
    ImGui::TableSetColumnIndex(0);
    
    // Setup flags for the tree node
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow 
                             | ImGuiTreeNodeFlags_SpanAllColumns; // Make the node span all columns
    
    if (node->children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // Leaf node specifics
    }
    
    // Selection state
    if (auto selectedEntity = m_selectedEntity.lock()) {
        bool isSelected = selectedEntity->isValid() && (selectedEntity->getID() == node->entity->getID());
        if (isSelected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
    }
    
    // Indentation
    float indentSize = depth * 20.0f; // Adjust indentation size as needed
    ImGui::Indent(indentSize);
    
    // Alternating row background color
    ImU32 rowBgColor = ImGui::ColorConvertFloat4ToU32(
        (rowIndex % 2 == 0) ? ImGuiPanelStyle::BACKGROUND_SECONDARY : ImGuiPanelStyle::BACKGROUND_PRIMARY
    );
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0 + (rowIndex % 2), rowBgColor);
    
    // Render the tree node itself
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)node->entity->getID(), flags, "%s", node->entityName.c_str());
    
    // Handle click for selection (only if not toggling open/close)
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        Rapture::GameEvents::onEntitySelected().publish(node->entity);
    }
    
    // Context Menu (Example)
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Properties")) {
 
        }
        // Add other actions like Delete, Duplicate, Add Child etc.
        if (ImGui::MenuItem("Delete Entity")) {
             // TODO: Implement entity deletion logic 
             Rapture::RP_WARN("Delete entity requested not implemented yet.");
        }
        ImGui::EndPopup();
    }
    
    ImGui::Unindent(indentSize);
    
    // --- Type Column ---    
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted("Entity"); // Simple type for now
    
    // --- Actions Column ---    
    ImGui::TableSetColumnIndex(2);
    // Add buttons here later, e.g., visibility toggle
    // ImGui::PushID((void*)(intptr_t)node->entity->getID()); // Ensure unique IDs for buttons
    // if (ImGui::Button("...")) {} 
    // ImGui::PopID();
    ImGui::TextUnformatted(""); // Placeholder
    
    // Recurse for children if the node is open and it's not a leaf
    if (!(flags & ImGuiTreeNodeFlags_Leaf) && nodeOpen) {
        for (const auto& childNode : node->children) {
            renderHierarchyRow(childNode, depth + 1, rowIndex);
        }
        ImGui::TreePop(); // Pop the node if it was opened and not a leaf
    }
}
