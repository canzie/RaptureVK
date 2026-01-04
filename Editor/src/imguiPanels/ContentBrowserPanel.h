#pragma once

#include <filesystem>
#include <functional>
#include <imgui.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "AssetManager/AssetManager.h"

enum class ContentBrowserMode {
    FILE,
    ASSET
};

class ContentBrowserPanel {
  public:
    ContentBrowserPanel();
    ~ContentBrowserPanel();
    void render();

    void setProjectAssetsPath(std::filesystem::path projectAssetsPath);

    // Callback for opening image viewer (set by ImGuiLayer)
    using OpenImageViewerCallback = std::function<void(Rapture::AssetHandle)>;
    void setOpenImageViewerCallback(OpenImageViewerCallback callback) { m_openImageViewerCallback = callback; }

  private:
    // Mode and navigation
    ContentBrowserMode m_currentMode = ContentBrowserMode::FILE;
    std::filesystem::path m_currentDirectory;
    std::filesystem::path m_projectAssetsPath = ""; // Default assets folder
    std::vector<std::filesystem::path> m_directoryHistory;
    int m_historyIndex = -1;

    // Search functionality
    char m_searchBuffer[256] = "";

    // Asset filtering
    Rapture::AssetType m_currentAssetFilter = Rapture::AssetType::NONE; // None means "All"

    // Asset selection and interaction
    std::set<Rapture::AssetHandle> m_selectedAssets;
    Rapture::AssetHandle m_hoveredAsset;
    float m_itemSize = 80.0f;

    // File browsing
    void renderFileHierarchy();
    void renderFileContent();
    void navigateBack();
    void navigateForward();

    // Asset browsing
    void renderAssetTypeHierarchy();
    void renderAssetContent();
    void renderAssetItem(Rapture::AssetHandle handle, const Rapture::AssetMetadata &metadata, float itemWidth);

    // UI helpers
    void renderTopPane();
    bool isSearchMatch(const std::string &name, const std::string &searchTerm) const;
    ImVec4 getAssetTypeColor(Rapture::AssetType type, bool isHovered) const;

    // Callback for opening image viewer
    OpenImageViewerCallback m_openImageViewerCallback;
};
