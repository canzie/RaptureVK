#ifndef RAPTURE__CONTENT_BROWSER_PANEL_H
#define RAPTURE__CONTENT_BROWSER_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/context_menu.h>
#include <components/tab_bar.h>
#include <components/tree_view.h>
#include <components/ui_scope.h>
#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/core/utils/TypeInfo.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "panels/Panel.h"

class IconButton;
class IconLabelButton;
class SearchField;

class ContentBrowserPanel : public Panel {
  public:
    std::string_view typeName() const override { return "ContentBrowserPanel"; }
    /**
     * @brief Creates the content browser
     * @param context The workspace the panel is built against
     * @param onDockRequested Run once the browser is asked to move into the workspace's layout, empty
     *        when it already lives there
     */
    explicit ContentBrowserPanel(const WorkspaceContext &context, std::function<void()> onDockRequested = {});

    void setContext(const WorkspaceContext &context) override;

    void refresh();
    void setBaseDirectory(const std::filesystem::path &path);

    /**
     * @brief Opens the folder an asset's file is in and selects the file
     * @param handle The asset to show, ignored if it has no file
     */
    void revealAsset(Rapture::AssetHandle handle);

  protected:
    void build(Amethyst::UIScope &scope) override;

  private:
    struct ContentItemComponents {
        Amethyst::Frame *container = nullptr;
        Amethyst::InvisibleButton *action = nullptr;
        Amethyst::ImageLabel *icon = nullptr;
        Amethyst::Frame *footer = nullptr;
        Amethyst::TextLabel *name = nullptr;
        Amethyst::TextInput *nameInput = nullptr;
        Amethyst::TextLabel *type = nullptr;
        Amethyst::Frame *typeBar = nullptr;
        Amethyst::UITooltip *tooltip = nullptr;
        Amethyst::EventConnection clickConnection;
        Amethyst::EventConnection doubleClickConnection;
        Amethyst::EventConnection contextMenuConnection;
        bool attached = false;
    };

    /**
     * @brief A titled section of the sidebar, folding away what sits under it
     */
    struct SidebarSection {
        Amethyst::InvisibleButton *header = nullptr;
        Amethyst::ImageLabel *chevron = nullptr;
        bool expanded = true;
    };

    // what the name being typed into a tile will do once it is confirmed
    enum ContentEditKind {
        CONTENT_EDIT_NONE,
        CONTENT_EDIT_FOLDER,
        CONTENT_EDIT_MODULE,
        CONTENT_EDIT_SCRIPT,
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
    void setupTopBar(void);

    /**
     * @brief Adds an island to the end of the toolbar, as wide as what is placed in it
     * @return The island
     */
    Amethyst::Frame *addToolbarIsland(void);

    /**
     * @brief Adds a full height line to the end of the toolbar, dividing the islands either side of it
     * @return The line
     */
    Amethyst::Frame *addToolbarSeparator(void);

    /**
     * @brief Adds a button showing an icon and a label in an island of its own
     * @param iconSvg The icon
     * @param label The label
     * @param styleClass Theme class the button is styled by
     * @return The button
     */
    IconLabelButton *addToolbarButtonIsland(std::string_view iconSvg, std::string_view label, std::string_view styleClass);

    void setupSideBar(void);

    /**
     * @brief Fills a sidebar section's header with its chevron, title and the line running to the edge
     * @param section The section the header belongs to
     * @param title The section's title
     * @param layoutOrder Where the section sits in the sidebar
     */
    void buildSidebarSection(SidebarSection &section, std::string_view title, uint32_t layoutOrder);

    /**
     * @brief Fits the folder tree to its shown rows, so Recent sits directly below it
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
     * @param objectClass The class a module holds, ignored by the other kinds
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
     * @brief Creates a module holding one authored object of a registered class
     * @param type The class the asset's root is
     * @param name The asset's name
     */
    void createModule(const Rapture::TypeInfo &type, std::string_view name);

    /**
     * @brief Creates a script holding an empty Lua body, and opens it
     * @param name The asset's name
     */
    void createScript(std::string_view name);

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
     * @brief Lays out a card's footer: its name alone or over its kind, and the strip in its type's colour
     * @param item The card being laid out
     * @param kind What the card is, shown under its name, empty to show the name alone
     * @param typeColor The colour of the strip along the footer's bottom edge, none for no strip
     */
    void layoutCardFooter(ContentItemComponents &item, std::string_view kind, std::optional<Amethyst::Color3> typeColor);

    void refreshFileBrowser(void);
    void buildDirectoryTree(void);
    void rebuildBreadcrumb(void);
    void updateStatus(size_t itemCount);

    void navigateToDirectory(const std::filesystem::path &path);
    void navigateBack(void);
    void navigateForward(void);
    void navigateUp(void);

    /**
     * @brief Greys out the back, forward and up buttons that have nowhere to go
     */
    void refreshNavigationButtons(void);

    /**
     * @brief Selects the folder tree row of the directory being shown
     */
    void selectCurrentDirectoryRow(void);

    void onSearchTextChanged(const std::string &text);

    ContentItemComponents &acquirePoolItem(size_t index);
    void releasePoolItems(size_t fromIndex);
    void applyItemSelection(ContentItemComponents &item, bool selected);
    void applyItemHover(ContentItemComponents &item, bool hovered);
    void selectItem(size_t index);

    void buildFilesSubtree(const std::filesystem::path &path, uint16_t depth);

    /**
     * @brief Adds a folder tree row naming a directory, which shows that directory when clicked
     * @param path The directory
     * @param depth The row's nesting depth
     */
    void addDirectoryRow(const std::filesystem::path &path, uint16_t depth);

  private:
    Amethyst::Frame *m_topBarPane = nullptr;
    IconLabelButton *m_addBtn = nullptr;
    IconLabelButton *m_importBtn = nullptr;
    IconButton *m_goBackBtn = nullptr;
    IconButton *m_goForwardBtn = nullptr;
    IconButton *m_goUpBtn = nullptr;
    IconButton *m_settingsBtn = nullptr;
    SearchField *m_searchField = nullptr;
    Amethyst::Frame *m_breadcrumbBar = nullptr;

    Amethyst::ContextMenu *m_contextMenu = nullptr;
    Amethyst::ContextMenu *m_addMenu = nullptr;

    Amethyst::Frame *m_sideBarPane = nullptr;
    SidebarSection m_projectSection;
    SidebarSection m_recentSection;
    Amethyst::TreeView *m_directoryTree = nullptr;
    Amethyst::UISizeConstraint *m_directoryTreeLimits = nullptr;
    std::vector<std::filesystem::path> m_directoryRowPaths;

    Amethyst::ScrollingFrame *m_contentContainer = nullptr;
    Amethyst::TextLabel *m_statusLabel = nullptr;

    std::vector<ContentItemComponents> m_contentItemPool;
    size_t m_selectedItem = SIZE_MAX;
    ContentEdit m_edit;

    std::function<void()> m_onDockRequested;
    Rapture::Scene *m_scene = nullptr;

    std::filesystem::path m_baseDirectory;
    std::filesystem::path m_currentDirectory;
    std::vector<std::filesystem::path> m_navigationHistory;
    std::vector<std::filesystem::path> m_shownEntryPaths;
    size_t m_historyIndex = 0;
    std::string m_searchFilter;
};

#endif // RAPTURE__CONTENT_BROWSER_PANEL_H
