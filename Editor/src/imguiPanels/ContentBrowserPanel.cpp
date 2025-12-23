#include "ContentBrowserPanel.h"

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "imguiPanelStyleLinear.h"
#include <algorithm>
#include <cctype>

ContentBrowserPanel::ContentBrowserPanel() : m_openImageViewerCallback(nullptr)
{
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

ContentBrowserPanel::~ContentBrowserPanel() {}

void ContentBrowserPanel::setProjectAssetsPath(std::filesystem::path projectAssetsPath)
{
    m_projectAssetsPath = projectAssetsPath;
}

void ContentBrowserPanel::render()
{
    RAPTURE_PROFILE_FUNCTION();

    std::string title = "Content Browser " + std::string(ICON_MD_FOLDER);
    ImGui::Begin(title.c_str());

    renderTopPane();

    // Main content area
    if (m_currentMode == ContentBrowserMode::FILE) {
        float leftPaneWidth = ImGui::GetContentRegionAvail().x * 0.25f;
        ImGui::BeginChild("LeftPane", ImVec2(leftPaneWidth, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Border);
        renderFileHierarchy();
        ImGui::EndChild();
        ImGui::SameLine();
    }

    ImGui::BeginChild("RightPane", ImVec2(0, 0), ImGuiChildFlags_Border);

    // Filter button on the left
    if (ImGui::Button("Filter")) {
        ImGui::OpenPopup("FilterPopup");
    }
    if (ImGui::BeginPopup("FilterPopup")) {
        ImGui::Text("Filter by type:");
        ImGui::Separator();
        if (ImGui::Selectable("All")) { /* TODO */
        }
        if (ImGui::Selectable("Texture")) { /* TODO */
        }
        if (ImGui::Selectable("Material")) { /* TODO */
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Search bar (centered)
    ImGui::Text("Search:");
    ImGui::SameLine();
    ImGui::InputText("##Search", m_searchBuffer, sizeof(m_searchBuffer));

    // Size slider on the right
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
    ImGui::PushItemWidth(120);
    ImGui::SliderFloat("##Size", &m_itemSize, 32.0f, 256.0f, "Size: %.0f");
    ImGui::PopItemWidth();

    ImGui::Separator();

    if (m_currentMode == ContentBrowserMode::FILE) {
        renderFileContent();
    } else {
        renderAssetContent();
    }

    ImGui::EndChild();

    ImGui::End();
}

void ContentBrowserPanel::renderTopPane()
{
    RAPTURE_PROFILE_FUNCTION();
    ImGui::BeginChild("TopPane", ImVec2(0, 36), ImGuiChildFlags_ResizeY | ImGuiChildFlags_Border);

    // Helper to vertically center the next item
    auto verticallyCenter = []() {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight()) * 0.5f);
    };

    verticallyCenter();
    ImGui::Text("Mode:");
    ImGui::SameLine();
    verticallyCenter();
    if (ImGui::RadioButton("File", m_currentMode == ContentBrowserMode::FILE)) {
        m_currentMode = ContentBrowserMode::FILE;
    }
    ImGui::SameLine();
    verticallyCenter();
    if (ImGui::RadioButton("Asset", m_currentMode == ContentBrowserMode::ASSET)) {
        m_currentMode = ContentBrowserMode::ASSET;
    }

    // Navigation buttons (only for file mode)
    if (m_currentMode == ContentBrowserMode::FILE) {
        ImGui::SameLine();
        verticallyCenter();
        ImGui::Separator();
        ImGui::SameLine();

        verticallyCenter();
        if (ImGui::Button("<") && m_historyIndex > 0) {
            navigateBack();
        }

        ImGui::SameLine();
        verticallyCenter();
        if (ImGui::Button(">") && m_historyIndex < m_directoryHistory.size() - 1) {
            navigateForward();
        }

        ImGui::SameLine();
        verticallyCenter();
        ImGui::Text("Path: %s", m_currentDirectory.string().c_str());
    }

    // Refresh button on the right side
    const char *refreshText = ICON_MD_REFRESH " Refresh";
    float refreshButtonWidth = ImGui::CalcTextSize(refreshText).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - refreshButtonWidth);
    verticallyCenter();
    if (ImGui::Button(refreshText, ImVec2(refreshButtonWidth, 0))) {
    }

    ImGui::EndChild();
}

void ContentBrowserPanel::renderFileHierarchy()
{
    RAPTURE_PROFILE_FUNCTION();
    ImGui::Text("File Hierarchy");
    ImGui::Separator();

    // Show directory tree starting from assets folder
    std::function<void(const std::filesystem::path &)> renderDirectoryTree = [&](const std::filesystem::path &path) {
        if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
            return;
        }

        try {
            for (const auto &entry : std::filesystem::directory_iterator(path)) {
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
        } catch (const std::filesystem::filesystem_error &ex) {
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

void ContentBrowserPanel::renderFileContent()
{
    ImGui::Text("Files in: %s", m_currentDirectory.filename().string().c_str());
    ImGui::Separator();
}

void ContentBrowserPanel::renderAssetTypeHierarchy()
{
    ImGui::Text("Asset Filters");
    ImGui::Separator();
}

void ContentBrowserPanel::renderAssetItem(Rapture::AssetHandle handle, const Rapture::AssetMetadata &metadata, float itemWidth)
{
    RAPTURE_PROFILE_FUNCTION();
    ImGui::PushID(static_cast<int>(handle));

    ImGui::BeginGroup();

    // -- Card background and selection --
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float itemHeight = itemWidth * 1.25f; // rectangular item
    ImVec2 p1 = ImVec2(p0.x + itemWidth, p0.y + itemHeight);
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // Card background
    drawList->AddRectFilled(p0, p1, IM_COL32(36, 36, 36, 255), 4.0f);

    // Image placeholder
    float imagePartHeight = itemWidth; // Square
    ImVec4 assetColor = getAssetTypeColor(metadata.assetType, false);
    drawList->AddRectFilled(p0, ImVec2(p0.x + itemWidth, p0.y + imagePartHeight), ImGui::ColorConvertFloat4ToU32(assetColor), 4.0f,
                            ImDrawFlags_RoundCornersTop);

    // -- Content --
    // Invisible button for interaction
    ImGui::InvisibleButton("##asset", ImVec2(itemWidth, itemHeight));

    bool isHovered = ImGui::IsItemHovered();

    if (isHovered) {
        drawList->AddRect(p0, p1, ImGui::GetColorU32(ImGuiPanelStyle::ACCENT_PRIMARY), 4.0f, ImDrawFlags_RoundCornersAll, 2.0f);
    }

    // Drag and drop source for textures
    if (metadata.assetType == Rapture::AssetType::Texture && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        RAPTURE_PROFILE_SCOPE("Texture Drag Drop Source");
        ImGui::SetDragDropPayload("TEXTURE_ASSET", &handle, sizeof(Rapture::AssetHandle));
        ImGui::Text("Texture: %s",
                    metadata.isDiskAsset() ? metadata.m_filePath.filename().string().c_str() : metadata.m_virtualName.c_str());
        ImGui::EndDragDropSource();
    }

    std::string contextMenuId = "AssetContextMenu_" + std::to_string(static_cast<uint64_t>(handle));
    if (metadata.assetType == Rapture::AssetType::Texture && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(contextMenuId.c_str());
    }

    if (ImGui::BeginPopup(contextMenuId.c_str())) {
        if (ImGui::MenuItem("Open in Image Viewer")) {
            if (m_openImageViewerCallback) {
                m_openImageViewerCallback(handle);
            }
        }
        ImGui::EndPopup();
    }

    if (isHovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(Rapture::AssetTypeToString(metadata.assetType).c_str());
        ImGui::EndTooltip();
    }

    // Name (clipped and centered)
    const std::string &name = metadata.isDiskAsset() ? metadata.m_filePath.filename().string() : metadata.m_virtualName;
    ImVec2 nameTextSize = ImGui::CalcTextSize(name.c_str(), nullptr, false, itemWidth - 8.0f);

    float textPosX = p0.x + (itemWidth - nameTextSize.x) * 0.5f;
    textPosX = std::max(p0.x + 4.0f, textPosX); // Clamp to avoid going off left edge

    ImGui::SetCursorScreenPos(ImVec2(textPosX, p0.y + imagePartHeight + 4));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + itemWidth - 8.0f);
    ImGui::TextWrapped("%s", name.c_str());
    ImGui::PopTextWrapPos();

    ImGui::EndGroup();

    ImGui::PopID();
}

void ContentBrowserPanel::renderAssetContent()
{
    RAPTURE_PROFILE_FUNCTION();

    // Grid settings
    float padding = 16.0f;
    float cellSize = m_itemSize + padding;

    float panelWidth = ImGui::GetContentRegionAvail().x;
    int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

    auto &loadedAssets = Rapture::AssetManager::getLoadedAssets();
    auto &assetRegistry = Rapture::AssetManager::getAssetRegistry();

    if (ImGui::BeginTable("AssetGrid", columnCount)) {
        for (const auto &[handle, asset] : loadedAssets) {
            if (assetRegistry.find(handle) == assetRegistry.end()) {
                continue;
            }
            Rapture::AssetMetadata metadata = assetRegistry.at(handle);
            if (metadata.assetType == Rapture::AssetType::None) {
                continue;
            }

            const std::string &name = metadata.isDiskAsset() ? metadata.m_filePath.filename().string() : metadata.m_virtualName;
            if (!isSearchMatch(name, m_searchBuffer)) {
                continue;
            }

            ImGui::TableNextColumn();
            renderAssetItem(handle, metadata, m_itemSize);
        }
        ImGui::EndTable();
    }
}

void ContentBrowserPanel::navigateBack()
{
    if (m_historyIndex > 0) {
        m_historyIndex--;
        m_currentDirectory = m_directoryHistory[m_historyIndex];
    }
}

void ContentBrowserPanel::navigateForward()
{
    if (m_historyIndex < m_directoryHistory.size() - 1) {
        m_historyIndex++;
        m_currentDirectory = m_directoryHistory[m_historyIndex];
    }
}

bool ContentBrowserPanel::isSearchMatch(const std::string &name, const std::string &searchTerm) const
{
    RAPTURE_PROFILE_FUNCTION();

    if (searchTerm.empty()) return true;

    std::string lowerName = name;
    std::string lowerSearch = searchTerm;

    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(), ::tolower);

    return lowerName.find(lowerSearch) != std::string::npos;
}

ImVec4 ContentBrowserPanel::getAssetTypeColor(Rapture::AssetType type, bool isHovered) const
{
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
