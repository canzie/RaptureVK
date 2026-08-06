#include "OutlinerPanel.h"
#include "Icons.h"
#include "events/GameEvents.h"
#include "layers/panels/AddSceneObjectMenu.h"
#include "layers/panels/components/tab_layouts.h"
#include "scenes/entities/Entity.h"

#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/text_input.h>
#include <components/text_label.h>
#include <components/ui_scope.h>
#include <memory>
#include <unordered_set>

#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

static void s_nameLabel(Amethyst::UIScope &s, const std::string &text, std::string_view className)
{
    Amethyst::UDim4 pd = {.left = Amethyst::UDim::fromOffset(2.0f)};
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
    std::unordered_set<const Rapture::Instance *> collapsed;
    for (uint32_t row = 0; row < m_rowInstances.size(); row++) {
        if (!m_treeView->isExpanded(row)) {
            collapsed.insert(m_rowInstances[row]);
        }
    }

    m_renameInput = nullptr;
    m_renamingInstance = nullptr;
    m_treeView->clear();
    m_rowInstances.clear();

    Rapture::Instance *sceneRoot = m_scene->root();
    if (sceneRoot == nullptr) {
        return;
    }

    Amethyst::TreeViewScope tvScope(*m_treeView);
    tvScope.columnsExplicit = true;

    for (const auto &child : sceneRoot->children()) {
        Rapture::Instance *instance = child.get();
        tvScope.row([this, instance](Amethyst::TreeRowScope &row) { buildInstanceTree(instance, row); });
    }

    for (uint32_t row = 0; row < m_rowInstances.size(); row++) {
        if (collapsed.contains(m_rowInstances[row])) {
            m_treeView->collapse(row);
        }
    }
}

void OutlinerPanel::buildInstanceTree(Rapture::Instance *instance, Amethyst::TreeRowScope &rowScope)
{
    if (instance == nullptr) {
        return;
    }

    m_rowInstances.push_back(instance);

    std::string instanceName(instance->name());
    std::string typeName(instance->type().name);

    rowScope.cell([instanceName](Amethyst::UIScope &s) { s_nameLabel(s, instanceName, "treeview-primary-column"); });
    rowScope.cell([typeName](Amethyst::UIScope &s) { s_nameLabel(s, typeName, "treeview-secondary-column"); });

    for (const auto &child : instance->children()) {
        Rapture::Instance *childInstance = child.get();
        rowScope.row([this, childInstance](Amethyst::TreeRowScope &childRow) { buildInstanceTree(childInstance, childRow); });
    }
}

Rapture::Instance *OutlinerPanel::instanceForRow(uint32_t row) const
{
    if (m_scene == nullptr || row >= m_rowInstances.size()) {
        return nullptr;
    }
    return m_rowInstances[row];
}

void OutlinerPanel::onRowClicked(uint32_t row)
{
    Rapture::Instance *instance = instanceForRow(row);
    if (instance != nullptr) {
        Rapture::GameEvents::onEntitySelected().publish(instance->entity());
    }
}

void OutlinerPanel::onRowRightClicked(uint32_t row, Amethyst::vec2 pos)
{
    Rapture::Instance *instance = instanceForRow(row);
    if (instance == nullptr) {
        return;
    }

    bool hasChildren = !instance->children().empty();

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.push_back(Amethyst::makeSubmenuItem("Add", AddSceneObjectMenu::buildItems(instance)));
    items.push_back(Amethyst::makeSeparatorItem());
    items.push_back(Amethyst::makeActionItem("Rename", [this, row, instance]() { startRename(row, instance); }));
    items.push_back(Amethyst::makeActionItem("Delete", [this, instance]() { requestDelete(instance, false); }));
    if (hasChildren) {
        items.push_back(Amethyst::makeActionItem("Delete (keep children)", [this, instance]() { requestDelete(instance, true); }));
    }

    showContextMenu(pos, std::move(items));
}

void OutlinerPanel::requestDelete(Rapture::Instance *instance, bool keepChildren)
{
    m_pendingDeleteInstance = instance;
    m_pendingDeleteKeepChildren = keepChildren;
}

void OutlinerPanel::applyPendingDelete()
{
    Rapture::Instance *instance = m_pendingDeleteInstance;
    bool keepChildren = m_pendingDeleteKeepChildren;
    m_pendingDeleteInstance = nullptr;
    m_pendingDeleteKeepChildren = false;

    if (m_scene == nullptr || instance == nullptr) {
        return;
    }

    Rapture::GameEvents::onEntityDeselected().publish(instance->entity());

    if (keepChildren) {
        Rapture::Instance *parent = instance->parent();
        while (parent != nullptr && !instance->children().empty()) {
            Rapture::Instance *child = instance->children().front().get();
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

void OutlinerPanel::buildNameCell(uint32_t row, Rapture::Instance *instance, const std::string &name, bool editing)
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

void OutlinerPanel::startRename(uint32_t row, Rapture::Instance *instance)
{
    if (instanceForRow(row) != instance) {
        return;
    }

    m_renamingInstance = instance;
    m_renameRow = row;
    buildNameCell(row, instance, std::string(instance->name()), true);
}

void OutlinerPanel::onRenameCommitted(Rapture::Instance *instance, const std::string &newName)
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

    Rapture::Instance *instance = m_renamingInstance;
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
