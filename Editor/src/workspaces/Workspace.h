#ifndef RAPTURE__WORKSPACE_H
#define RAPTURE__WORKSPACE_H

#include "EntitySelection.h"
#include "context/EditorContext.h"
#include "panels/Panel.h"

#include <amethyst/Amethyst.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

class IconLabelButton;

/**
 * @brief What an edit made in a viewport acts on
 */
enum EditorMode {
    EDITOR_MODE_OBJECT, ///< the objects in the scene, moved and selected whole
    EDITOR_MODE_EDIT,   ///< the data an object is made of, such as a skeleton's rest pose
    EDITOR_MODE_POSE,   ///< the pose an object is shown in, leaving the data it is made of alone
    EDITOR_MODE_COUNT
};

constexpr std::string_view EDITOR_MODE_LABELS[EDITOR_MODE_COUNT] = {"Object Mode", "Edit Mode", "Pose Mode"};

class Workspace {
  public:
    /**
     * @brief Creates a workspace, which builds nothing until it is attached
     * @param context What the whole editor reaches
     */
    explicit Workspace(const EditorContext &context);
    virtual ~Workspace();

    Workspace(const Workspace &) = delete;
    Workspace &operator=(const Workspace &) = delete;

    /**
     * @brief Builds this workspace into a tab appended to the workspace bar
     * @param tabBar The bar the tab is appended to
     */
    void attach(Amethyst::TabBar &tabBar);

    /**
     * @brief Text shown on this workspace's tab
     */
    virtual std::string_view tabLabel() const = 0;

    /**
     * @brief SVG markup shown left of the tab's label, empty to omit it
     */
    virtual std::string_view tabIcon() const { return {}; }

    /**
     * @brief Color the tab's icon is tinted with
     * @return The color, empty to keep the color every tab icon has
     */
    virtual std::optional<Amethyst::Color4> tabIconColor() const { return std::nullopt; }

    /**
     * @brief The class this workspace is, which a command can find it by
     */
    virtual std::string_view typeName() const = 0;

    /**
     * @brief The number the editor knows this workspace by, unique among the workspaces opened this run
     */
    uint32_t id() const { return m_id; }

    /**
     * @brief Gives this workspace the number the editor knows it by
     * @param id The number, unique among the workspaces opened this run
     */
    void assignId(uint32_t id) { m_id = id; }

    /**
     * @brief Docks a panel into this workspace
     * @param panel The panel to take over
     * @param tabBar The bar the panel's tab is added to
     * @return The panel, which this workspace now owns
     */
    template <typename T>
    T *addPanel(std::unique_ptr<T> panel, Amethyst::TabBar *tabBar)
    {
        T *added = panel.get();
        adoptPanel(std::move(panel), tabBar);
        return added;
    }

    /**
     * @brief Docks a panel into a region of its own
     * @param panel The panel to take over
     * @param zone Side of the content dock the panel is docked on
     * @return The panel, which this workspace now owns
     */
    template <typename T>
    T *addPanel(std::unique_ptr<T> panel, Amethyst::DockZone zone)
    {
        return addPanel(std::move(panel), m_contentDock->dockNewRegion(zone));
    }

    virtual void onUpdate(float dt);

    /**
     * @brief Tells this workspace whether its tab is the one in front
     * @param active True while it is the selected tab of the workspace bar
     */
    void setActive(bool active);

    bool isActive() const { return m_active; }

    /**
     * @brief Tells this workspace where its tab's content sits in the window
     * @param position Top left corner of the content, in window pixels
     * @param size Size of the content, in window pixels
     */
    void setContentArea(Amethyst::vec2 position, Amethyst::vec2 size);

    /**
     * @brief Lays this workspace's contents out by the sizes the editor config currently holds.
     *
     * Called as the workspace is built and again whenever those sizes change.
     */
    virtual void resolveLayout();

    /**
     * @brief Writes out how this workspace's panels are arranged, under its own name
     */
    void saveDockLayout() const;

    EditorMode mode() const { return m_mode; }

    /**
     * @brief Puts this workspace into a mode, firing onModeChanged when it differs from the current one
     * @param mode The mode to enter
     */
    void setMode(EditorMode mode);

    Amethyst::DockingLayer *getContentDock() const { return m_contentDock; }

    const std::vector<std::unique_ptr<Panel>> &getPanels() const { return m_panels; }
    const WorkspaceContext &getContext() const { return m_context; }

    /**
     * @brief The frame this workspace fills, which is the content of the tab it lives in
     */
    Amethyst::Frame *getContainer() const { return m_container; }

  public:
    /**
     * @brief Fires with the mode just entered
     */
    Rapture::EventSignal<void(EditorMode)> onModeChanged;

  protected:
    /**
     * @brief Fills this workspace's container, called once as it is attached
     */
    virtual void build() = 0;

    /**
     * @brief Fills this workspace's hotbar, called once as it is attached
     */
    virtual void buildHotbar() = 0;

    /**
     * @brief The name this workspace's dock arrangement is stored under
     * @return The name, which has to be stable across runs
     */
    virtual std::string dockLayoutName() const = 0;

    /**
     * @brief Puts this workspace's panels back in the arrangement they were last saved in
     */
    void restoreDockLayout();

    /**
     * @brief Destroys every panel, for a workspace whose own members outlive them by less than the base does
     */
    void destroyPanels();

    /**
     * @brief Adds a button to the end of the hotbar, as wide as the text it holds
     * @param label Text on the button
     * @param onClick What to run when the button is pressed
     * @return The button
     */
    Amethyst::TextButton *addHotbarButton(std::string_view label, std::function<void()> onClick);

    /**
     * @brief Adds a button showing an icon and a label to the end of the hotbar, as wide as both
     * @param label Text on the button
     * @param iconSvg Icon shown before the label
     * @param onClick What to run when the button is pressed
     * @return The button
     */
    IconLabelButton *addHotbarButton(std::string_view label, std::string_view iconSvg, std::function<void()> onClick);

    /**
     * @brief Adds a button showing an icon and a label to the end of the hotbar, as wide as both
     * @param label Text on the button
     * @param iconSvg Icon shown before the label
     * @param styleClass Theme class the button is styled by, in place of the hotbar's own
     * @param onClick What to run when the button is pressed
     * @return The button
     */
    IconLabelButton *addHotbarButton(std::string_view label, std::string_view iconSvg, std::string_view styleClass,
                                     std::function<void()> onClick);

    /**
     * @brief Adds a full height line to the end of the hotbar, dividing the items before it from the
     *        items after it as separate islands
     */
    void addHotbarSeparator();

    /**
     * @brief Adds an item the hotbar has no ready made shape for
     * @param width Width of the item's frame
     * @param build Fills the item's frame, called once
     * @return The item's frame
     */
    Amethyst::Frame *addHotbarItem(float width, const std::function<void(Amethyst::UIScope &)> &build);

    /**
     * @brief Adds an item the hotbar has no ready made shape for, at a height other than the hotbar's controls
     * @param size Width and height of the item's frame
     * @param build Fills the item's frame, called once
     * @return The item's frame
     */
    Amethyst::Frame *addHotbarItem(Amethyst::vec2 size, const std::function<void(Amethyst::UIScope &)> &build);

    /**
     * @brief Adds an item the hotbar has no ready made shape for, as wide as what it holds
     * @param build Fills the item's frame, called once
     * @return The item's frame
     */
    Amethyst::Frame *addFittedHotbarItem(const std::function<void(Amethyst::UIScope &)> &build);

    const EditorConfig &config() const;

    /**
     * @brief What decides how wide a hotbar item is
     */
    enum class HotbarItemKind {
        FIXED,         ///< as wide as it was asked to be
        LABELLED,      ///< as wide as the text it holds
        ICON_LABELLED, ///< as wide as the icon and text it holds
        FITTED,        ///< as wide as the children it holds
        SEPARATOR,     ///< a full height line between islands
    };

    /**
     * @brief One thing sitting in the hotbar: the island it fills and the control inside it
     */
    struct HotbarItem {
        Amethyst::UIObject *island = nullptr;
        Amethyst::UIObject *control = nullptr;
        float width = 0.0f;
        HotbarItemKind kind = HotbarItemKind::FIXED;
        float height = 0.0f;
    };

  protected:
    EditorContext m_editor;
    WorkspaceContext m_context;
    EntitySelection m_selection;
    EditorMode m_mode = EDITOR_MODE_OBJECT;

    Amethyst::Frame *m_container = nullptr;
    Amethyst::Frame *m_hotbar = nullptr;
    Amethyst::UIListLayout *m_hotbarLayout = nullptr;
    Amethyst::DockingLayer *m_contentDock = nullptr;
    std::vector<std::unique_ptr<Panel>> m_panels;
    uint32_t m_id = 0;
    uint32_t m_nextPanelId = 1;

  private:
    /**
     * @brief Fills a tab's content frame with the hotbar and content dock every workspace has
     * @param container The tab content frame this workspace fills
     */
    void buildContainer(Amethyst::Frame &container);

    /**
     * @brief Takes a panel over, building it if it is not built yet, and docks its tab
     * @param panel The panel to take over
     * @param tabBar The bar the panel's tab is added to
     */
    void adoptPanel(std::unique_ptr<Panel> panel, Amethyst::TabBar *tabBar);

    /**
     * @brief Tells every panel whether it is the one on screen
     */
    void refreshShownPanels();

    /**
     * @brief Puts the content dock in the part of the content area the hotbar leaves free
     */
    void placeContentDock();

    /**
     * @brief Sizes and places one hotbar item by the paddings the editor config holds
     * @param item The item to lay out
     */
    void resolveHotbarItem(const HotbarItem &item) const;

    /**
     * @brief Adds an island to the end of the hotbar for one control
     * @return The island's frame
     */
    Amethyst::Frame *addHotbarIsland();


    std::vector<HotbarItem> m_hotbarItems;
    Amethyst::vec2 m_contentAreaPosition = {0.0f, 0.0f};
    Amethyst::vec2 m_contentAreaSize = {0.0f, 0.0f};
    bool m_active = false;
    bool m_teardown = false;
};

#endif // RAPTURE__WORKSPACE_H
