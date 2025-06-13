#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <set>
#include <memory>

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
        Rapture::AssetType m_currentAssetFilter = Rapture::AssetType::None; // None means "All"
        
        // Asset selection and interaction
        std::set<Rapture::AssetHandle> m_selectedAssets;
        Rapture::AssetHandle m_hoveredAsset;
        
        
        // File browsing
        void renderFileHierarchy();
        void renderFileContent();
        void navigateBack();
        void navigateForward();
        
        // Asset browsing
        void renderAssetTypeHierarchy();
        void renderAssetContent();
        

        // UI helpers
        void renderTopPane();
        bool isSearchMatch(const std::string& name, const std::string& searchTerm) const;
        ImVec4 getAssetTypeColor(Rapture::AssetType type, bool isHovered) const;
};
