#include "OutlinerPanel.h"
#include "EntitySelection.h"
#include "Icons.h"
#include "layers/panels/AddSceneObjectMenu.h"
#include "layers/panels/components/asset_visuals.h"
#include "layers/panels/components/context_menus.h"
#include "layers/panels/components/tab_layouts.h"
#include "scene/World.h"
#include "scene/instances/SceneObject.h"
#include "scene/instances/scene_components/VisibilityComponent.h"
#include "core/ecs/entity_accessor.h"

#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/text_input.h>
#include <components/text_label.h>
#include <components/ui_scope.h>
#include <memory>
#include <unordered_set>

#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

static constexpr float ROW_ICON_SIZE = 13.0f;
static constexpr float ROW_ICON_PAD = 2.0f;
static constexpr float ROW_ICON_GAP = 5.0f;

static bool s_showsInOutliner(const Rapture::SceneObject *object)
{
    Rapture::VisibilityComponent *visibility = object->component<Rapture::VisibilityComponent>();
    return visibility == nullptr || visibility->inOutliner;
}

static void s_nameLabel(Amethyst::UIScope &s, const std::string &text, std::string_view className,
                        const SceneObjectIcon *icon = nullptr)
{
    float textLeft = 2.0f;

    if (icon != nullptr) {
        s.imageLabel({
            .classes = {std::string(icon->styleClass)},
            .base = {.anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                     .interactable = false,
                     .position = Amethyst::UDim2(0.0f, ROW_ICON_PAD, 0.5f, 0.0f),
                     .size = Amethyst::UDim2::fromOffset(ROW_ICON_SIZE, ROW_ICON_SIZE)},
            .style = {.backgroundTransparency = 1.0f},
            .svg = icon->svg,
        });
        textLeft = ROW_ICON_PAD + ROW_ICON_SIZE + ROW_ICON_GAP;
    }

    Amethyst::UDim4 pd = {.left = Amethyst::UDim::fromOffset(textLeft)};
    s.textLabel(
        {
            .classes = {std::string(className)},
            .base = {.padding = pd, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
            .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
            .label = text,
        },
        [](Amethyst::TextLabelScope &l) { l.component.propagate(Amethyst::INTERACTION_CATEGORY_ALL); });
}

OutlinerPanel::OutlinerPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context) : Panel("Outliner", context)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        m_scrollingFrame = nullptr;
        m_treeView = nullptr;
        m_contextMenu = nullptr;
        m_renameInput = nullptr;
    });
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    Amethyst::UIScope(*m_root)
        .frame({
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
        })
        .scrollingFrame(
            {
                .classes = {"panel"},
                .base =
                    {
                        .clipsDescendants = true,
                        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
                    },
                .scroll =
                    {
                        .scrollAxis = Amethyst::ScrollAxis::Y,
                        .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                        .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                    },
            },
            [this](Amethyst::ScrollingFrameScope &sf) {
                m_scrollingFrame = &sf.component;
                sf.treeView(
                    {
                        .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                        .treeView = {.cellPadding = Amethyst::UDim4{{0.0f, 0.0f}},
                                     .showColumnSeparators = false,
                                     .disclosureTriangleSize = 24.0f,
                                     .showHeader = true},
                    },
                    [this](Amethyst::TreeViewScope &tv) {
                        m_treeView = &tv.component;
                        tv.column({.header = "Name", .weight = 3.0f, .labelPadding = Amethyst::UDim4{.left = {0.0f, 0.0f}}});
                        tv.column("Type", 1.0f);
                        tv.column("", 0.5f);
                    });
            });

    m_treeView->onRowClicked = [this](uint32_t row) { onRowClicked(row); };
    m_treeView->onRowRightClicked = [this](uint32_t row, Amethyst::vec2 pos) { onRowRightClicked(row, pos); };

    m_contextMenu = m_root->add<Amethyst::ContextMenu>();
    m_contextMenu->setContextMenuProperties({.itemHoverBackground = COL_MENU_HOVER});
    m_contextMenu->setRowFactories({.separator = [] { return std::make_unique<ViewportContextMenuSIV>(); }});

    icon = Icons::SVG_LAYERS;
    attach(tabBar, std::move(root));

    setScene(context.scene);
}

OutlinerPanel::~OutlinerPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void OutlinerPanel::setScene(Rapture::Scene *scene)
{
    m_scene = scene;
    m_hasScene = (scene != nullptr);

    m_hierarchyChangedConn.disconnect();
    if (m_hasScene) {
        // deferred, the signal reaches here from inside a context menu callback
        m_hierarchyChangedConn = scene->onHierarchyChanged.connect([this]() { m_pendingRefresh = true; });
    }

    if (m_treeView == nullptr) {
        return;
    }

    if (m_hasScene) {
        m_treeView->setBaseProperties({.visible = true});
        refresh();
    } else {
        m_treeView->setBaseProperties({.visible = false});
        m_treeView->clear();
        m_rowInstances.clear();
    }
}

void OutlinerPanel::onUpdate(float dt)
{
    (void)dt;

    if (m_pendingDeleteInstance != nullptr) {
        applyPendingDelete();
    }

    if (m_pendingRefresh) {
        m_pendingRefresh = false;
        refresh();
    }

    if (m_pendingRenameCommit) {
        applyPendingRename();
    }

    if (m_renameInput != nullptr && !m_renameInput->isFocused()) {
        m_renameInput->focus();
        m_renameInput->selectAll();
    }
}

void OutlinerPanel::refresh()
{
    if (!m_hasScene || !m_scene) {
        return;
    }

    // a rebuilt row starts expanded, so what the user collapsed is carried across by instance
    std::unordered_set<const Rapture::SceneObject *> collapsed;
    for (uint32_t row = 0; row < m_rowInstances.size(); row++) {
        if (!m_treeView->isExpanded(row)) {
            collapsed.insert(m_rowInstances[row]);
        }
    }

    m_renameInput = nullptr;
    m_renamingInstance = nullptr;
    m_treeView->clear();
    m_rowInstances.clear();

    Rapture::SceneObject *sceneRoot = m_scene->root();
    if (sceneRoot == nullptr) {
        return;
    }

    Amethyst::TreeViewScope tvScope(*m_treeView);
    tvScope.columnsExplicit = true;

    for (const auto &child : sceneRoot->children()) {
        Rapture::SceneObject *instance = child.get();
        if (!s_showsInOutliner(instance)) {
            continue;
        }
        tvScope.row([this, instance](Amethyst::TreeRowScope &row) { buildInstanceTree(instance, row); });
    }

    for (uint32_t row = 0; row < m_rowInstances.size(); row++) {
        if (collapsed.contains(m_rowInstances[row])) {
            m_treeView->collapse(row);
        }
    }
}

void OutlinerPanel::buildInstanceTree(Rapture::SceneObject *instance, Amethyst::TreeRowScope &rowScope)
{
    if (instance == nullptr) {
        return;
    }

    m_rowInstances.push_back(instance);

    std::string instanceName(instance->name());
    std::string typeName(instance->type().name);

    SceneObjectIcon icon = SceneObject_iconForClass(&instance->type());
    rowScope.cell([instanceName, icon](Amethyst::UIScope &s) { s_nameLabel(s, instanceName, "treeview-primary-column", &icon); });
    rowScope.cell([typeName](Amethyst::UIScope &s) { s_nameLabel(s, typeName, "treeview-secondary-column"); });

    for (const auto &child : instance->children()) {
        Rapture::SceneObject *childInstance = child.get();
        if (!s_showsInOutliner(childInstance)) {
            continue;
        }
        rowScope.row([this, childInstance](Amethyst::TreeRowScope &childRow) { buildInstanceTree(childInstance, childRow); });
    }
}

Rapture::SceneObject *OutlinerPanel::instanceForRow(uint32_t row) const
{
    if (m_scene == nullptr || row >= m_rowInstances.size()) {
        return nullptr;
    }
    return m_rowInstances[row];
}

void OutlinerPanel::onRowClicked(uint32_t row)
{
    Rapture::SceneObject *instance = instanceForRow(row);
    if (instance != nullptr && m_selection != nullptr) {
        m_selection->select(instance->accessor());
    }
}

void OutlinerPanel::onRowRightClicked(uint32_t row, Amethyst::vec2 pos)
{
    Rapture::SceneObject *instance = instanceForRow(row);
    if (instance == nullptr) {
        return;
    }

    bool hasChildren = !instance->children().empty();

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    if (m_selection != nullptr) {
        items.push_back(Amethyst::makeSubmenuItem(
            "Add", AddSceneObjectMenu_buildItems(instance, *m_selection, SCENE_OBJECT_SCOPE_LEVEL)));
    }
    items.push_back(ViewportContextMenuSID::create());
    items.push_back(Amethyst::makeActionItem("Rename", [this, row, instance]() { startRename(row, instance); }));
    items.push_back(Amethyst::makeActionItem("Delete", [this, instance]() { requestDelete(instance, false); }));
    if (hasChildren) {
        items.push_back(Amethyst::makeActionItem("Delete (keep children)", [this, instance]() { requestDelete(instance, true); }));
    }

    showContextMenu(pos, std::move(items));
}

void OutlinerPanel::requestDelete(Rapture::SceneObject *instance, bool keepChildren)
{
    m_pendingDeleteInstance = instance;
    m_pendingDeleteKeepChildren = keepChildren;
}

void OutlinerPanel::applyPendingDelete()
{
    Rapture::SceneObject *instance = m_pendingDeleteInstance;
    bool keepChildren = m_pendingDeleteKeepChildren;
    m_pendingDeleteInstance = nullptr;
    m_pendingDeleteKeepChildren = false;

    if (m_scene == nullptr || instance == nullptr) {
        return;
    }

    if (m_selection != nullptr && m_selection->entity().getEntity() == instance->entity()) {
        m_selection->clear();
    }

    if (keepChildren) {
        Rapture::SceneObject *parent = instance->parent();
        while (parent != nullptr && !instance->children().empty()) {
            Rapture::SceneObject *child = instance->children().front().get();
            parent->addChild(instance->removeChild(child));
        }
    }

    m_scene->destroyInstance(instance);

    m_treeView->selectedRow = Amethyst::TreeView::NO_ROW_SELECTION;
    refresh();
}

void OutlinerPanel::showContextMenu(Amethyst::vec2 pos, std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items)
{
    if (m_contextMenu == nullptr) {
        return;
    }
    m_contextMenu->setItems(std::move(items));
    m_contextMenu->showAt(pos);
}

void OutlinerPanel::buildNameCell(uint32_t row, Rapture::SceneObject *instance, const std::string &name, bool editing)
{
    if (m_treeView == nullptr) {
        return;
    }

    if (editing) {
        auto input = std::make_unique<Amethyst::TextInput>();
        Amethyst::TextInput *raw = input.get();
        m_renameInput = raw;
        raw->addClass("property-input-field");
        raw->setText(name);
        raw->setBaseProperties({
            .padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(2.0f)},
            .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
            .zIndex = 2,
        });
        raw->setTextInputProperties({.text = {.fontSize = 14.0f, .textYAlignment = Amethyst::TextYAlignment::CENTER}});
        raw->onEnterPressed = [this, instance, raw]() { onRenameCommitted(instance, raw->getText()); };
        raw->onFocusLost = [this, instance, raw]() { onRenameCommitted(instance, raw->getText()); };
        m_treeView->setCell(row, 0, std::move(input));
    } else {
        auto label = std::make_unique<Amethyst::TextLabel>();
        label->setText(name);
        label->setClasses({"treeview-primary-column"});
        label->setBaseProperties(
            {.padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(2.0f)}, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
        label->setBaseStyleProperties({.backgroundTransparency = 1.0f});
        label->setTextStyleProperties({.textYAlignment = Amethyst::TextYAlignment::CENTER});
        label->propagate(Amethyst::INTERACTION_CATEGORY_ALL);
        m_treeView->setCell(row, 0, std::move(label));
    }
}

void OutlinerPanel::startRename(uint32_t row, Rapture::SceneObject *instance)
{
    if (instanceForRow(row) != instance) {
        return;
    }

    m_renamingInstance = instance;
    m_renameRow = row;
    buildNameCell(row, instance, std::string(instance->name()), true);
}

void OutlinerPanel::onRenameCommitted(Rapture::SceneObject *instance, const std::string &newName)
{
    if (m_renamingInstance != instance) {
        return;
    }
    m_pendingRenameName = newName;
    m_pendingRenameCommit = true;
}

void OutlinerPanel::applyPendingRename()
{
    m_pendingRenameCommit = false;

    Rapture::SceneObject *instance = m_renamingInstance;
    uint32_t row = m_renameRow;
    m_renamingInstance = nullptr;
    m_renameInput = nullptr;

    if (instance == nullptr || instanceForRow(row) != instance) {
        return;
    }

    if (!m_pendingRenameName.empty()) {
        instance->setName(m_pendingRenameName);
    }

    buildNameCell(row, instance, std::string(instance->name()), false);
}
