#ifndef RAPTURE__CONTENT_BROWSER_PANEL_H
#define RAPTURE__CONTENT_BROWSER_PANEL_H

#include "assets/asset_manager/AssetCommon.h"
#include "core/events/EventSignal.h"
#include "core/utils/TypeInfo.h"
#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/tab_bar.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "layers/panels/Panel.h"

class ContentBrowserPanel : public Panel {
  public:
    ContentBrowserPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context);
    ContentBrowserPanel(Amethyst::PopupScope &scope, const PanelServices &services);
    ~ContentBrowserPanel();
    ContentBrowserPanel(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel &operator=(const ContentBrowserPanel &) = delete;
    ContentBrowserPanel(ContentBrowserPanel &&) = delete;
    ContentBrowserPanel &operator=(ContentBrowserPanel &&) = delete;

    virtual void setContext(const WorkspaceContext &context);

    void refresh();
    void setBaseDirectory(const std::filesystem::path &path);
    void setScene(Rapture::Scene *scene) { m_scene = scene; }

    Rapture::EventSignal<void()> onDockInLayout;

  private:
    struct ContentItemComponents {
        Amethyst::Frame *container = nullptr;
        Amethyst::InvisibleButton *action = nullptr;
        Amethyst::Frame *thumbWell = nullptr;
        Amethyst::ImageLabel *icon = nullptr;
        Amethyst::Frame *footer = nullptr;
        Amethyst::TextLabel *name = nullptr;
        Amethyst::TextInput *nameInput = nullptr;
        Amethyst::TextLabel *type = nullptr;
        Amethyst::Frame *typeBar = nullptr;
        Amethyst::UITooltip *tooltip = nullptr;
        bool attached = false;
    };

    // what the name being typed into a tile will do once it is confirmed
    enum ContentEditKind {
        CONTENT_EDIT_NONE,
        CONTENT_EDIT_FOLDER,
        CONTENT_EDIT_SCENE_OBJECT,
        CONTENT_EDIT_RENAME,
        CONTENT_EDIT_COUNT
    };

    struct ContentEdit {
        ContentEditKind kind = CONTENT_EDIT_NONE;
        size_t itemIndex = SIZE_MAX;
        const Rapture::TypeInfo *objectClass = nullptr;
        std::filesystem::path target;
        std::string initialName;
    };

  private:
    void buildContent(void);

    void setupTopBar(void);
    void setupSideBar(void);

    /**
     * @brief Sizes the sidebar sections so that a collapsed one takes only its header and the ones below it move up
     */
    void layoutSideBar(void);

    void setupContentArea(void);
    void setupContextMenu(void);

    void showContextMenu(Amethyst::vec2 pos, std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items);

    /**
     * @brief The menu items creating a folder or an asset in the current directory
     * @return The items, ready to hand to a ContextMenu
     */
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> buildAddMenuItems();

    /**
     * @brief The submenu items of the advanced section, one per asset category
     * @return The items, ready to hand to a ContextMenu
     */
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> buildAdvancedAddItems();

    void showAddMenu(Amethyst::vec2 pos);

    /**
     * @brief Puts a placeholder tile in the grid for the user to name, which creates nothing until confirmed
     * @param kind What the confirmed name will create
     * @param objectClass The class a scene object asset holds, ignored by the other kinds
     */
    void beginCreate(ContentEditKind kind, const Rapture::TypeInfo *objectClass = nullptr);

    /**
     * @brief Puts an existing item's tile into its naming state
     * @param index The item's index in the content pool
     * @param target The file or folder the name belongs to
     */
    void beginRename(size_t index, const std::filesystem::path &target);

    /**
     * @brief Swaps an item's name label for a field holding that name
     * @param item The item being named
     * @param initialName The name the field starts with
     */
    void startEditing(ContentItemComponents &item, const std::string &initialName);

    /**
     * @brief Acts on the name that was typed, doing nothing at all if it is empty
     */
    void commitEdit();

    void createFolder(std::string_view name);

    /**
     * @brief Creates a scene object asset holding one authored object of a registered class
     * @param type The class the asset's root is
     * @param name The asset's name
     */
    void createSceneObject(const Rapture::TypeInfo &type, std::string_view name);

    /**
     * @brief Renames a file or folder on disk
     * @param target The path to rename
     * @param name The name it takes, extension excluded
     */
    void renameItem(const std::filesystem::path &target, std::string_view name);

    /**
     * @brief The context menu actions specific to an asset type, before the shared rename/delete items
     * @param type The asset's type
     * @param handle The asset's handle
     * @return The type-specific menu items, empty if the type has none
     */
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> assetActions(Rapture::AssetType type,
                                                                               Rapture::AssetHandle handle);

    /**
     * @brief Sizes a card's footer so that it takes the type bar's strip on a card that has no type bar
     * @param item The card being laid out
     * @param hasTypeBar Whether the card shows a type accent bar along its bottom edge
     */
    void layoutCardFooter(ContentItemComponents &item, bool hasTypeBar);

    void refreshFileBrowser(void);
    void buildDirectoryTree(void);
    void rebuildBreadcrumb(void);
    void updateStatus(size_t itemCount);

    void navigateToDirectory(const std::filesystem::path &path);
    void navigateBack(void);
    void navigateForward(void);

    void onSearchTextChanged(const std::string &text);

    ContentItemComponents &acquirePoolItem(size_t index);
    void releasePoolItems(size_t fromIndex);
    void applyItemSelection(ContentItemComponents &item, bool selected);
    void applyItemHover(ContentItemComponents &item, bool hovered);
    void selectItem(size_t index);

    void buildFilesSubtree(const std::filesystem::path &path, uint16_t depth);

  private:
    Amethyst::Frame *m_topBarPane = nullptr;
    Amethyst::TextButton *m_addBtn = nullptr;
    Amethyst::TextButton *m_importBtn = nullptr;
    Amethyst::ImageButton *m_goBackBtn = nullptr;
    Amethyst::ImageButton *m_goForwardBtn = nullptr;
    Amethyst::ImageButton *m_settingsBtn = nullptr;
    Amethyst::Frame *m_breadcrumbBar = nullptr;

    Amethyst::ContextMenu *m_contextMenu = nullptr;
    Amethyst::ContextMenu *m_addMenu = nullptr;

    Amethyst::Frame *m_sideBarPane = nullptr;
    Amethyst::CollapsibleHeader *m_projectSection = nullptr;
    Amethyst::CollapsibleHeader *m_recentSection = nullptr;
    Amethyst::TreeView *m_directoryTree = nullptr;

    Amethyst::Frame *m_contentPane = nullptr;
    Amethyst::Frame *m_searchBar = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;
    Amethyst::ScrollingFrame *m_contentContainer = nullptr;
    Amethyst::TextLabel *m_statusLabel = nullptr;

    std::vector<ContentItemComponents> m_contentItemPool;
    size_t m_selectedItem = SIZE_MAX;
    ContentEdit m_edit;

    bool m_isDocked = false;
    Rapture::Scene *m_scene = nullptr;

    std::filesystem::path m_baseDirectory;
    std::filesystem::path m_currentDirectory;
    std::vector<std::filesystem::path> m_navigationHistory;
    size_t m_historyIndex = 0;
    std::string m_searchFilter;
};

#endif // RAPTURE__CONTENT_BROWSER_PANEL_H
