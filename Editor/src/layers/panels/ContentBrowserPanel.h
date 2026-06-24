#ifndef RAPTURE__CONTENT_BROWSER_PANEL_H
#define RAPTURE__CONTENT_BROWSER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

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
    explicit ContentBrowserPanel(Amethyst::PopupScope &scope);
    ~ContentBrowserPanel();
    ContentBrowserPanel(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel &operator=(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel(ContentBrowserPanel &&) = delete;
    ContentBrowserPanel &operator=(ContentBrowserPanel &&) = delete;

    void refresh();
    void setBaseDirectory(const std::filesystem::path &path);

  private:
    void buildContent();

    void setupTopBar();
    void setupSideBar();
    void setupContentArea();
    void setupContextMenu();

    void showContextMenu(Amethyst::vec2 pos, std::vector<Amethyst::ContextMenuItem> items);

    void refreshAssetBrowser();
    void refreshFileBrowser();
    void buildDirectoryTree();
    void rebuildBreadcrumb();
    void updateStatus(size_t itemCount);

    void navigateToDirectory(const std::filesystem::path &path);
    void navigateBack();
    void navigateForward();

    void showAssets();

    void onSearchTextChanged(const std::string &text);

    struct ContentItemComponents {
        Amethyst::Frame *container = nullptr;
        Amethyst::InvisibleButton *action = nullptr;
        Amethyst::Frame *thumbWell = nullptr;
        Amethyst::ImageLabel *icon = nullptr;
        Amethyst::Frame *footer = nullptr;
        Amethyst::TextLabel *name = nullptr;
        Amethyst::Frame *typeBar = nullptr;
        bool attached = false;
    };

    ContentItemComponents &acquirePoolItem(size_t index);
    void releasePoolItems(size_t fromIndex);
    void applyItemSelection(ContentItemComponents &item, bool selected);
    void applyItemHover(ContentItemComponents &item, bool hovered);
    void selectItem(size_t index);

    void buildFilesSubtree(const std::filesystem::path &path, uint16_t depth);

  private:
    Amethyst::Frame *m_root = nullptr;

    Amethyst::Frame *m_topBarPane = nullptr;
    Amethyst::TextButton *m_addBtn = nullptr;
    Amethyst::TextButton *m_importBtn = nullptr;
    Amethyst::ImageButton *m_goBackBtn = nullptr;
    Amethyst::ImageButton *m_goForwardBtn = nullptr;
    Amethyst::ImageButton *m_settingsBtn = nullptr;
    Amethyst::Frame *m_breadcrumbBar = nullptr;

    Amethyst::ContextMenu *m_contextMenu = nullptr;

    Amethyst::Frame *m_sideBarPane = nullptr;
    Amethyst::CollapsibleHeader *m_projectHeader = nullptr;
    Amethyst::ScrollingFrame *m_directoryTreeContainer = nullptr;
    Amethyst::TreeView *m_directoryTree = nullptr;

    Amethyst::Frame *m_contentPane = nullptr;
    Amethyst::Frame *m_searchBar = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;
    Amethyst::ScrollingFrame *m_contentContainer = nullptr;
    Amethyst::TextLabel *m_statusLabel = nullptr;

    std::vector<ContentItemComponents> m_contentItemPool;
    size_t m_selectedItem = SIZE_MAX;

    BrowseMode m_browseMode = BrowseMode::ASSETS;
    std::filesystem::path m_baseDirectory;
    std::filesystem::path m_currentDirectory;
    std::vector<std::filesystem::path> m_navigationHistory;
    size_t m_historyIndex = 0;
    std::string m_searchFilter;
};

#endif // RAPTURE__CONTENT_BROWSER_PANEL_H
