#include "BrowserPanel.h"

#include "Components/Components.h"
#include "Components/FogComponent.h"
#include "Components/HierarchyComponent.h"
#include "Components/IndirectLightingComponent.h"
#include "imguiPanelStyleLinear.h"

#include "AssetManager/AssetManager.h"
#include "Events/GameEvents.h"
#include "Logging/TracyProfiler.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/SceneManager.h"

#define CHILD_INDENT_SIZE 10.0f

BrowserPanel::BrowserPanel()
{
    m_sceneActivatedListenerId =
        Rapture::GameEvents::onSceneActivated().addListener([this](std::shared_ptr<Rapture::Scene> scene) { m_scene = scene; });

    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) { m_selectedEntity = *entity; });

    auto currentActiveScene = Rapture::SceneManager::getInstance().getActiveScene();
    if (currentActiveScene) {
        m_scene = currentActiveScene;
    }
}

BrowserPanel::~BrowserPanel()
{

    Rapture::GameEvents::onSceneActivated().removeListener(m_sceneActivatedListenerId);
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
}

void BrowserPanel::render()
{
    RAPTURE_PROFILE_FUNCTION();

    ImGui::Begin("Entity Browser");

    if (auto scene = m_scene.lock()) {
        auto &registry = scene->getRegistry();

        auto view = registry.view<Rapture::TagComponent>();
        uint32_t entityCount = static_cast<uint32_t>(view.size());

        // Display total entities
        ImGui::Text("Total Entities: %d", entityCount);

        // Refresh button on the right
        ImGui::SameLine(ImGui::GetWindowWidth() - 80.0f); // Adjust position as needed
        if (ImGui::Button(ICON_MD_REFRESH)) {
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
        ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg;

        if (ImGui::BeginTable("EntityHierarchyTable", columnCount, tableFlags)) {
            ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int rowIndex = 0;
            for (const auto &rootNode : m_hierarchyRoots) {
                if (rootNode && rootNode->entity.isValid()) {
                    renderHierarchyRow(rootNode, 0, rowIndex);
                }
            }

            ImGui::EndTable();
        }

        renderContextMenuEmpty(m_scene.lock());
    } else {
        ImGui::Text("No active scene available");
    }

    ImGui::End();
}

void BrowserPanel::buildHierarchyCache()
{

    m_hierarchyRoots.clear();

    if (!m_scene.lock()) {
        return;
    }

    auto scene = m_scene.lock();

    auto &registry = scene->getRegistry();
    auto view = registry.view<Rapture::TagComponent>();

    // Map to store all nodes temporarily
    std::unordered_map<uint32_t, std::shared_ptr<HierarchyNode>> entityNodeMap;
    // Set to keep track of entities that have been added as children
    std::unordered_set<uint32_t> childrenEntities;

    // First pass: Create nodes for all entities
    for (auto entityHandle : view) {
        Rapture::Entity entity(entityHandle, scene.get());
        if (!entity.isValid()) continue;

        std::string entityName = entity.getComponent<Rapture::TagComponent>().tag + " " + std::to_string(entity.getID());

        if (entity.hasComponent<Rapture::LightComponent>()) {
            entityName = std::string(ICON_MD_SUNNY) + " " + entityName;
        }

        entityNodeMap[entity.getID()] = std::make_shared<HierarchyNode>(entity, entityName);
    }

    // Second pass: Build parent-child relationships
    for (auto entityHandle : view) {
        Rapture::Entity entity(entityHandle, scene.get());
        if (!entity.isValid()) continue;

        uint32_t entityID = entity.getID();
        if (entity.hasComponent<Rapture::HierarchyComponent>()) {
            auto &nodeComp = entity.getComponent<Rapture::HierarchyComponent>();
            if (nodeComp.hasParent()) {
                Rapture::Entity parent = nodeComp.parent;
                uint32_t parentID = parent.getID();

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

    // Third pass: Add root nodes (entities that are not children of anyone)
    for (auto const &[id, node] : entityNodeMap) {
        if (childrenEntities.find(id) == childrenEntities.end()) {
            m_hierarchyRoots.push_back(node);
        }
    }
}

void BrowserPanel::renderHierarchyRow(const std::shared_ptr<HierarchyNode> &node, int depth, int &rowIndex)
{
    if (!node || !node->entity.isValid()) {
        return;
    }

    ImGui::TableNextRow();
    rowIndex++;

    ImGui::TableSetColumnIndex(0);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;

    if (m_renamingEntity != node->entity) {
        flags |= ImGuiTreeNodeFlags_SpanAllColumns;
    } else {
        flags |= ImGuiTreeNodeFlags_AllowOverlap;
    }

    if (node->children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // Leaf node specifics
    }

    // Selection state
    if (m_selectedEntity.isValid()) {
        bool isSelected = m_selectedEntity.isValid() && (m_selectedEntity.getID() == node->entity.getID());
        if (isSelected) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
    }

    // Indentation
    float indentSize = depth * CHILD_INDENT_SIZE;
    ImGui::Indent(indentSize);

    // Apply custom styling for selected tree nodes
    ImGui::PushStyleColor(ImGuiCol_Header, ImGuiPanelStyle::ACCENT_PRIMARY);

    bool nodeOpen = false;
    if (m_renamingEntity == node->entity) {
        nodeOpen = ImGui::TreeNodeEx((void *)(intptr_t)node->entity.getID(), flags, "%s", "");

    } else {
        // Render the tree node itself
        nodeOpen = ImGui::TreeNodeEx((void *)(intptr_t)node->entity.getID(), flags, "%s", node->entityName.c_str());
    }

    ImGui::PopStyleColor();

    // Handle click for selection (only if not toggling open/close)
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        Rapture::GameEvents::onEntitySelected().publish(std::make_shared<Rapture::Entity>(node->entity));
    }

    if (m_renamingEntity == node->entity) {
        ImGui::SameLine();

        // Style the input field to be seamless with the table row
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0)); // Remove padding to match text height
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);      // Remove border
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        if (ImGui::InputText("##xx", m_entityRenameBuffer, sizeof(m_entityRenameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank |
                                 ImGuiInputTextFlags_AutoSelectAll)) {
            node->entity.getComponent<Rapture::TagComponent>().tag = std::string(m_entityRenameBuffer);
            m_needsHierarchyRebuild = true;
            m_renamingEntity = Rapture::Entity::null();
        }

        ImGui::PopStyleVar(2); // Pop the 2 style vars (FramePadding, FrameBorderSize)
        ImGui::PopStyleColor();
    }

    // Context Menu
    if (ImGui::BeginPopupContextItem()) {

        if (ImGui::MenuItem("Rename Entity")) {
            // close the contextmenu, and let the user edit the name rendered
            // when the user presses enter, get the tag component and update the name

            strncpy(m_entityRenameBuffer, node->entityName.c_str(), sizeof(m_entityRenameBuffer) - 1);
            m_entityRenameBuffer[sizeof(m_entityRenameBuffer) - 1] = '\0';
            m_renamingEntity = node->entity;
        }

        // Add other actions like Delete, Duplicate, Add Child etc.
        if (ImGui::MenuItem("Delete Entity")) {
            // TODO: Implement entity deletion logic
            Rapture::RP_WARN("Delete entity requested not implemented yet.");
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("Add Component")) {
            renderAddComponentMenu(node->entity);
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Properties")) {
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
    // ImGui::PushID((void*)(intptr_t)node->entity.getID()); // Ensure unique IDs for buttons
    // if (ImGui::Button("...")) {}
    // ImGui::PopID();
    ImGui::Text(ICON_MD_VISIBILITY);

    // Recurse for children if the node is open and it's not a leaf
    if (!(flags & ImGuiTreeNodeFlags_Leaf) && nodeOpen) {
        for (const auto &childNode : node->children) {
            renderHierarchyRow(childNode, depth + 1, rowIndex);
        }
        ImGui::TreePop(); // Pop the node if it was opened and not a leaf
    }
}

void BrowserPanel::renderContextMenuEmpty(std::shared_ptr<Rapture::Scene> scene)
{
    // Only show background context menu if we're not hovering over any item
    // and the mouse is within the window bounds
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("ContextMenuEmpty");
    }

    if (ImGui::BeginPopup("ContextMenuEmpty")) {
        if (ImGui::MenuItem("Create Entity")) {
            auto entity = scene->createEntity("New Entity");
            if (entity.isValid()) {
                Rapture::GameEvents::onEntitySelected().publish(std::make_shared<Rapture::Entity>(entity));
            }
        }
        if (ImGui::MenuItem("Create Cube")) {
            auto entity = scene->createCube("New Cube");
            if (entity.isValid()) {
                Rapture::GameEvents::onEntitySelected().publish(std::make_shared<Rapture::Entity>(entity));
            }
        }
        if (ImGui::MenuItem("Create Sphere")) {
            auto entity = scene->createSphere("New Sphere");
            if (entity.isValid()) {
                Rapture::GameEvents::onEntitySelected().publish(std::make_shared<Rapture::Entity>(entity));
            }
        }
        ImGui::EndPopup();
    }
}

void BrowserPanel::renderAddComponentMenu(Rapture::Entity entity)
{
    if (!entity.isValid()) {
        return;
    }

    // Helper lambda to safely add a component
    auto tryAddComponent = [&entity](auto &&addFunc, const char *name) {
        try {
            addFunc();
            return true;
        } catch (const Rapture::EntityException &e) {
            // Component already exists, skip it
            (void)e;
            return false;
        } catch (const std::exception &e) {
            Rapture::RP_ERROR("Failed to add component {}: {}", name, e.what());
            return false;
        }
    };

    // Mesh Component
    if (!entity.hasComponent<Rapture::MeshComponent>()) {
        if (ImGui::MenuItem("Mesh Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::MeshComponent>(); }, "Mesh Component");
        }
    }

    // Light Component
    if (!entity.hasComponent<Rapture::LightComponent>()) {
        if (ImGui::MenuItem("Light Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::LightComponent>(); }, "Light Component");
        }
    }

    // Camera Component
    if (!entity.hasComponent<Rapture::CameraComponent>()) {
        if (ImGui::MenuItem("Camera Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::CameraComponent>(); }, "Camera Component");
        }
    }

    // Camera Controller Component
    if (!entity.hasComponent<Rapture::CameraControllerComponent>()) {
        if (ImGui::MenuItem("Camera Controller Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::CameraControllerComponent>(); },
                            "Camera Controller Component");
        }
    }

    // Fog Component
    if (!entity.hasComponent<Rapture::FogComponent>()) {
        if (ImGui::MenuItem("Fog Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::FogComponent>(); }, "Fog Component");
        }
    }

    // Indirect Lighting Component
    if (!entity.hasComponent<Rapture::IndirectLightingComponent>()) {
        if (ImGui::MenuItem("Indirect Lighting Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::IndirectLightingComponent>(); },
                            "Indirect Lighting Component");
        }
    }

    // Bounding Box Component
    if (!entity.hasComponent<Rapture::BoundingBoxComponent>()) {
        if (ImGui::MenuItem("Bounding Box Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::BoundingBoxComponent>(); }, "Bounding Box Component");
        }
    }

    // Skybox Component
    if (!entity.hasComponent<Rapture::SkyboxComponent>()) {
        if (ImGui::MenuItem("Skybox Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::SkyboxComponent>(); }, "Skybox Component");
        }
    }
}
