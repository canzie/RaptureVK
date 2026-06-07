#ifndef RAPTURE__CONTENT_BROWSER_PANEL_H
#define RAPTURE__CONTENT_BROWSER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/tree_view.h>

#include <filesystem>
#include <string>
#include <vector>

enum class BrowseMode {
    ASSETS,
    FILES
};

#include "layers/panels/Panel.h"

class ContentBrowserPanel : public Panel {
  public:
    ContentBrowserPanel(Amethyst::TabBar *tabBar);
    ~ContentBrowserPanel();
    ContentBrowserPanel(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel &operator=(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel(ContentBrowserPanel &&) = delete;
    ContentBrowserPanel &operator=(ContentBrowserPanel &&) = delete;

    void onUpdate(float dt) override;
    void refresh();
    void setBaseDirectory(const std::filesystem::path &path);

  private:
    void setupTopBar();
    void setupSideBar();
    void setupContentArea();

    void refreshAssetBrowser();
    void refreshFileBrowser();
    void refreshDirectoryTree();

    void navigateToDirectory(const std::filesystem::path &path);
    void navigateBack();
    void navigateForward();

    void onSearchTextChanged(const std::string &text);
    void toggleBrowseMode();

    struct ContentItemComponents {
        Amethyst::Frame *container = nullptr;
        Amethyst::InvisibleButton *action = nullptr;
        Amethyst::ImageLabel *thumbnail = nullptr;
        Amethyst::Frame *typeIndicator = nullptr;
        Amethyst::TextLabel *name = nullptr;
        bool attached = false;
    };

    ContentItemComponents &acquirePoolItem(size_t index);
    void releasePoolItems(size_t fromIndex);
    void buildDirectoryTree(const std::filesystem::path &path, uint16_t depth);

  private:
    Amethyst::TabBar *m_hostTabBar = nullptr;
    Amethyst::Frame *m_root = nullptr;

    Amethyst::Frame *m_topBarPane = nullptr;
    Amethyst::TextButton *m_addBtn = nullptr;
    Amethyst::TextButton *m_importBtn = nullptr;
    Amethyst::TextButton *m_goBackBtn = nullptr;
    Amethyst::TextButton *m_goForwardBtn = nullptr;

    Amethyst::Frame *m_sideBarPane = nullptr;
    Amethyst::TextButton *m_modeToggleBtn = nullptr;
    Amethyst::ScrollingFrame *m_directoryTreeContainer = nullptr;
    Amethyst::TreeView *m_directoryTree = nullptr;

    Amethyst::Frame *m_contentPane = nullptr;
    Amethyst::Frame *m_contentOptionsBar = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;
    Amethyst::ScrollingFrame *m_contentContainer = nullptr;

    std::vector<ContentItemComponents> m_contentItemPool;

    BrowseMode m_browseMode = BrowseMode::ASSETS;
    std::filesystem::path m_baseDirectory;
    std::filesystem::path m_currentDirectory;
    std::vector<std::filesystem::path> m_navigationHistory;
    size_t m_historyIndex = 0;
    std::string m_searchFilter;
};

#endif // RAPTURE__CONTENT_BROWSER_PANEL_H
