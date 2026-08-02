#include "OutlinerPanel.h"
#include "Icons.h"
#include "components/Components.h"
#include "components/HierarchyComponent.h"
#include "events/GameEvents.h"
#include "layers/panels/components/tab_layouts.h"
#include "scenes/entities/Entity.h"

#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/text_input.h>
#include <components/text_label.h>
#include <components/ui_scope.h>
#include <memory>

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

    if (m_treeView == nullptr) {
        return;
    }

    if (m_hasScene) {
        m_treeView->setBaseProperties({.visible = true});
        refresh();
    } else {
        m_treeView->setBaseProperties({.visible = false});
        m_treeView->clear();
        m_rowEntities.clear();
    }
}

void OutlinerPanel::onUpdate(float dt)
{
    (void)dt;

    if (m_pendingDeleteEntityId != UINT32_MAX) {
        applyPendingDelete();
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

    m_renameInput = nullptr;
    m_renamingEntityId = UINT32_MAX;
    m_treeView->clear();
    m_rowEntities.clear();

    Amethyst::TreeViewScope tvScope(*m_treeView);
    tvScope.columnsExplicit = true;

    m_scene->getRegistry().view<Rapture::TagComponent>().each([this, &tvScope](auto entityHandle, auto &tag) {
        Rapture::Entity entity(entityHandle, m_scene);

        bool isRoot = !entity.hasComponent<Rapture::HierarchyComponent>() ||
                      !entity.getComponent<Rapture::HierarchyComponent>().parent.isValid();
        if (!isRoot) {
            return;
        }

        tvScope.row([this, entity](Amethyst::TreeRowScope &row) { buildEntityTree(entity, row); });
    });
}

void OutlinerPanel::buildEntityTree(Rapture::Entity entity, Amethyst::TreeRowScope &rowScope)
{
    if (!entity.isValid()) {
        return;
    }

    m_rowEntities.push_back(entity.getID());

    std::string entityName = entity.getComponent<Rapture::TagComponent>().tag;

    rowScope.cell([entityName](Amethyst::UIScope &s) { s_nameLabel(s, entityName, "treeview-primary-column"); });
    rowScope.cell([](Amethyst::UIScope &s) { s_nameLabel(s, "Entity", "treeview-secondary-column"); });

    if (entity.hasComponent<Rapture::HierarchyComponent>()) {
        const auto &hierarchy = entity.getComponent<Rapture::HierarchyComponent>();
        for (const auto &child : hierarchy.children) {
            if (child.isValid()) {
                rowScope.row([this, child](Amethyst::TreeRowScope &childRow) { buildEntityTree(child, childRow); });
            }
        }
    }
}

Rapture::Entity OutlinerPanel::entityForRow(uint32_t row) const
{
    if (m_scene == nullptr || row >= m_rowEntities.size()) {
        return Rapture::Entity();
    }
    return Rapture::Entity(m_rowEntities[row], m_scene);
}

void OutlinerPanel::onRowClicked(uint32_t row)
{
    Rapture::Entity entity = entityForRow(row);
    if (entity.isValid()) {
        Rapture::GameEvents::onEntitySelected().publish(entity);
    }
}

void OutlinerPanel::onRowRightClicked(uint32_t row, Amethyst::vec2 pos)
{
    Rapture::Entity entity = entityForRow(row);
    if (!entity.isValid()) {
        return;
    }

    uint32_t entityId = entity.getID();

    bool hasChildren = false;
    if (auto *hierarchy = entity.tryGetComponent<Rapture::HierarchyComponent>()) {
        hasChildren = hierarchy->hasChildren();
    }

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.push_back(Amethyst::makeActionItem("Rename", [this, row, entityId]() { startRename(row, entityId); }));
    items.push_back(Amethyst::makeActionItem("Delete", [this, entityId]() { requestDelete(entityId, false); }));
    if (hasChildren) {
        items.push_back(Amethyst::makeActionItem("Delete (keep children)", [this, entityId]() { requestDelete(entityId, true); }));
    }

    showContextMenu(pos, std::move(items));
}

void OutlinerPanel::requestDelete(uint32_t entityId, bool keepChildren)
{
    m_pendingDeleteEntityId = entityId;
    m_pendingDeleteKeepChildren = keepChildren;
}

void OutlinerPanel::applyPendingDelete()
{
    uint32_t entityId = m_pendingDeleteEntityId;
    bool keepChildren = m_pendingDeleteKeepChildren;
    m_pendingDeleteEntityId = UINT32_MAX;
    m_pendingDeleteKeepChildren = false;

    if (m_scene == nullptr) {
        return;
    }

    Rapture::Entity entity(entityId, m_scene);
    if (!entity.isValid()) {
        return;
    }

    if (keepChildren) {
        Rapture::HierarchyComponent::destroyKeepChildren(entity);
    } else {
        Rapture::HierarchyComponent::destroyHierarchy(entity);
    }

    Rapture::GameEvents::onEntityDeselected().publish(entity);
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

void OutlinerPanel::buildNameCell(uint32_t row, uint32_t entityId, const std::string &name, bool editing)
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
        raw->onEnterPressed = [this, entityId, raw]() { onRenameCommitted(entityId, raw->getText()); };
        raw->onFocusLost = [this, entityId, raw]() { onRenameCommitted(entityId, raw->getText()); };
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

void OutlinerPanel::startRename(uint32_t row, uint32_t entityId)
{
    Rapture::Entity entity = entityForRow(row);
    if (!entity.isValid() || entity.getID() != entityId) {
        return;
    }

    std::string name = entity.getComponent<Rapture::TagComponent>().tag;

    m_renamingEntityId = entityId;
    m_renameRow = row;
    buildNameCell(row, entityId, name, true);
}

void OutlinerPanel::onRenameCommitted(uint32_t entityId, const std::string &newName)
{
    if (m_renamingEntityId != entityId) {
        return;
    }
    m_pendingRenameName = newName;
    m_pendingRenameCommit = true;
}

void OutlinerPanel::applyPendingRename()
{
    m_pendingRenameCommit = false;

    uint32_t entityId = m_renamingEntityId;
    uint32_t row = m_renameRow;
    m_renamingEntityId = UINT32_MAX;
    m_renameInput = nullptr;

    Rapture::Entity entity = entityForRow(row);
    if (!entity.isValid() || entity.getID() != entityId) {
        return;
    }

    auto &tag = entity.getComponent<Rapture::TagComponent>();
    if (!m_pendingRenameName.empty()) {
        tag.tag = m_pendingRenameName;
    }

    buildNameCell(row, entityId, tag.tag, false);
}
