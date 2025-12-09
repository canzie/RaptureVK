#include "ContentBrowserPanel.h"

#include "imguiPanelStyleLinear.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include <algorithm>
#include <cctype>

ContentBrowserPanel::ContentBrowserPanel() {
    // Initialize current directory to the assets folder
    m_currentDirectory = m_projectAssetsPath;
    
    // Create assets directory if it doesn't exist
    if (!std::filesystem::exists(m_projectAssetsPath) && m_projectAssetsPath != "") {
        std::filesystem::create_directories(m_projectAssetsPath);
    }
    
    // Initialize navigation history
    m_directoryHistory.push_back(m_currentDirectory);
    m_historyIndex = 0;
    

}

ContentBrowserPanel::~ContentBrowserPanel() {

}

void ContentBrowserPanel::setProjectAssetsPath(std::filesystem::path projectAssetsPath) {
    m_projectAssetsPath = projectAssetsPath;
    
}

void ContentBrowserPanel::render() {
    RAPTURE_PROFILE_FUNCTION();

    std::string title = "Content Browser " + std::string(ICON_MD_FOLDER);
    ImGui::Begin(title.c_str());

    renderTopPane();
    
    // Main content area
    float leftPaneWidth = ImGui::GetContentRegionAvail().x * 0.25f;
    ImGui::BeginChild("LeftPane", ImVec2(leftPaneWidth, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Border);
    
    if (m_currentMode == ContentBrowserMode::FILE) {
        renderFileHierarchy();
    } else {
        renderAssetTypeHierarchy();
    }
    
    ImGui::EndChild();
    
    ImGui::SameLine();

    ImGui::BeginChild("RightPane", ImVec2(0, 0), ImGuiChildFlags_Border);
    
    // Search bar at the top of the right pane
    ImGui::Text("Search:");
    ImGui::SameLine();
    ImGui::InputText("##Search", m_searchBuffer, sizeof(m_searchBuffer));
    ImGui::Separator();
    
    if (m_currentMode == ContentBrowserMode::FILE) {
        renderFileContent();
    } else {
        renderAssetContent();
    }
    
    ImGui::EndChild();

    ImGui::End();
}

void ContentBrowserPanel::renderTopPane() {
    RAPTURE_PROFILE_FUNCTION();
    ImGui::BeginChild("TopPane", ImVec2(0, 20), ImGuiChildFlags_ResizeY | ImGuiChildFlags_Border);
    
    // Mode selection
    ImGui::Text("Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("File", m_currentMode == ContentBrowserMode::FILE)) {
        m_currentMode = ContentBrowserMode::FILE;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Asset", m_currentMode == ContentBrowserMode::ASSET)) {
        m_currentMode = ContentBrowserMode::ASSET;
    }
    
    // Navigation buttons (only for file mode)
    if (m_currentMode == ContentBrowserMode::FILE) {
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        
        // Back button
        if (ImGui::Button("<") && m_historyIndex > 0) {
            navigateBack();
        }
        
        ImGui::SameLine();
        
        // Forward button
        if (ImGui::Button(">") && m_historyIndex < m_directoryHistory.size() - 1) {
            navigateForward();
        }
        
        ImGui::SameLine();
        
        // Current path display
        ImGui::Text("Path: %s", m_currentDirectory.string().c_str());
    }
    
    // Refresh button on the right side
    float refreshButtonWidth = 60.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - refreshButtonWidth);
    if (ImGui::Button(ICON_MD_REFRESH, ImVec2(refreshButtonWidth, 0))) {
    }
    
    ImGui::EndChild();
}

void ContentBrowserPanel::renderFileHierarchy() {
    RAPTURE_PROFILE_FUNCTION();
    ImGui::Text("File Hierarchy");
    ImGui::Separator();
    
    // Show directory tree starting from assets folder
    std::function<void(const std::filesystem::path&)> renderDirectoryTree = 
        [&](const std::filesystem::path& path) {
            if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
                return;
            }
            
            try {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (entry.is_directory()) {
                        std::string name = entry.path().filename().string();
                        
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (entry.path() == m_currentDirectory) {
                            flags |= ImGuiTreeNodeFlags_Selected;
                        }

                        std::string label = std::string(ICON_MD_FOLDER) + " " + name;
                        bool node_open = ImGui::TreeNodeEx(entry.path().string().c_str(), flags, "%s", label.c_str());

                        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                            m_currentDirectory = entry.path();
                             // Truncate history if we navigated back and then chose a new path
                            if (m_historyIndex < m_directoryHistory.size() - 1) {
                                m_directoryHistory.resize(m_historyIndex + 1);
                            }
                            m_directoryHistory.push_back(m_currentDirectory);
                            m_historyIndex++;
                        }
                        
                        if (node_open) {
                            renderDirectoryTree(entry.path());
                            ImGui::TreePop();
                        }
                    }
                }
            } catch (const std::filesystem::filesystem_error& ex) {
                // Handle permission errors gracefully
                Rapture::RP_ERROR("ContentBrowserPanel::renderFileHierarchy - {0}", ex.what());
            }
        };

    // Special handling for the root assets path to be open by default
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (m_projectAssetsPath == m_currentDirectory) {
        rootFlags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string rootLabel = std::string(ICON_MD_FOLDER) + " Assets";
    bool rootNodeOpen = ImGui::TreeNodeEx(m_projectAssetsPath.string().c_str(), rootFlags, "%s", rootLabel.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_currentDirectory = m_projectAssetsPath;
        if (m_historyIndex < m_directoryHistory.size() - 1) {
            m_directoryHistory.resize(m_historyIndex + 1);
        }
        m_directoryHistory.push_back(m_currentDirectory);
        m_historyIndex++;
    }

    if (rootNodeOpen) {
        renderDirectoryTree(m_projectAssetsPath);
        ImGui::TreePop();
    }
}

void ContentBrowserPanel::renderFileContent() {
    ImGui::Text("Files in: %s", m_currentDirectory.filename().string().c_str());
    ImGui::Separator();
    
    
}

void ContentBrowserPanel::renderAssetTypeHierarchy() {
    ImGui::Text("Asset Filters");
    ImGui::Separator();
    

}

// Simulated file entry
struct FileItem {
    std::string name;
    bool isFolder;
};



void ContentBrowserPanel::renderAssetContent() {
    RAPTURE_PROFILE_FUNCTION();
    const float padding = 32.0f;
    const float iconSize = 128.0f;
    const float minItemWidth = iconSize + padding;
    const float textHeight = ImGui::GetTextLineHeightWithSpacing();
    const float itemHeight = iconSize + textHeight + padding;

    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    int itemsPerRow = std::max(1, static_cast<int>(panelSize.x / minItemWidth));
    float actualItemWidth = panelSize.x / itemsPerRow;


    auto& loadedAssets = Rapture::AssetManager::getLoadedAssets();
    auto& assetRegistry = Rapture::AssetManager::getAssetRegistry();

    int itemIndex = 0;
    for (const auto& [handle, asset] : loadedAssets) {

        if (assetRegistry.find(handle) == assetRegistry.end()) {
            continue;
        }
        Rapture::AssetMetadata metadata = assetRegistry.at(handle);
        if (metadata.m_assetType == Rapture::AssetType::None) {
            continue;
        }

        if (metadata.isDiskAsset() && !isSearchMatch(metadata.m_filePath.filename().string(), m_searchBuffer)) {
            continue;
        } else if (metadata.isVirtualAsset() && !isSearchMatch(metadata.m_virtualName, m_searchBuffer)) {
            continue;
        }

        ImGui::PushID(itemIndex++);

        ImGui::BeginGroup();

        // Center the icon
        float iconPadding = (actualItemWidth - iconSize) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconPadding);



        ImVec4 iconColor = getAssetTypeColor(metadata.m_assetType, false);
        ImVec4 iconColorHovered = getAssetTypeColor(metadata.m_assetType, true);
        ImGui::PushStyleColor(ImGuiCol_Button, iconColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, iconColorHovered);

        bool hovered = false;

        if (ImGui::Button("##Icon", ImVec2(iconSize, iconSize))) {
            // You can handle click events here
        }
        hovered = ImGui::IsItemHovered();

        ImGui::PopStyleColor(2);

        // Set up drag source for texture assets
        if (metadata.m_assetType == Rapture::AssetType::Texture && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            RAPTURE_PROFILE_SCOPE("Texture Drag Drop Source");
            // Set the payload to contain the asset handle
            ImGui::SetDragDropPayload("TEXTURE_ASSET", &handle, sizeof(Rapture::AssetHandle));
            
            // Display preview while dragging
            ImGui::Text("Texture: %s", metadata.isDiskAsset() ? 
                metadata.m_filePath.filename().string().c_str() : 
                metadata.m_virtualName.c_str());
            
            ImGui::EndDragDropSource();
        }

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(Rapture::AssetTypeToString(metadata.m_assetType).c_str());
            ImGui::EndTooltip();
        }

        // Filename text (clipped and centered)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconPadding);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSize);
        if (metadata.isDiskAsset()) {
            ImGui::TextWrapped("%s", metadata.m_filePath.filename().string().c_str());
        } else {
            ImGui::TextWrapped("%s", metadata.m_virtualName.c_str());
        }
        ImGui::PopTextWrapPos();

        ImGui::EndGroup();

        float spacingX = actualItemWidth - ImGui::GetItemRectSize().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spacingX);

        // Row management
        if ((itemIndex % itemsPerRow) != 0)
            ImGui::SameLine();

        ImGui::PopID();
    }



}



void ContentBrowserPanel::navigateBack() {
    if (m_historyIndex > 0) {
        m_historyIndex--;
        m_currentDirectory = m_directoryHistory[m_historyIndex];
    }
}

void ContentBrowserPanel::navigateForward() {
    if (m_historyIndex < m_directoryHistory.size() - 1) {
        m_historyIndex++;
        m_currentDirectory = m_directoryHistory[m_historyIndex];
    }
}

bool ContentBrowserPanel::isSearchMatch(const std::string& name, const std::string& searchTerm) const {
    RAPTURE_PROFILE_FUNCTION();

    if (searchTerm.empty()) return true;
    
    std::string lowerName = name;
    std::string lowerSearch = searchTerm;
    
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);
    
    return lowerName.find(lowerSearch) != std::string::npos;
}



ImVec4 ContentBrowserPanel::getAssetTypeColor(Rapture::AssetType type, bool isHovered) const {
    switch (type) {
        case Rapture::AssetType::Texture:
            return isHovered ? ImGuiPanelStyle::GRUVBOX_ORANGE_BRIGHT : ImGuiPanelStyle::GRUVBOX_ORANGE_NORMAL;
        case Rapture::AssetType::Shader:
            return isHovered ? ImGuiPanelStyle::GRUVBOX_RED_BRIGHT : ImGuiPanelStyle::GRUVBOX_RED_NORMAL;
        case Rapture::AssetType::Material:
            return isHovered ? ImGuiPanelStyle::GRUVBOX_PURPLE_BRIGHT : ImGuiPanelStyle::GRUVBOX_PURPLE_NORMAL;
        default:
            return isHovered ? ImGuiPanelStyle::ACCENT_PRIMARY : ImGuiPanelStyle::ACCENT_PRIMARY;
    }
}

