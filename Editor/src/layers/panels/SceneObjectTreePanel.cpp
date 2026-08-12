#include "SceneObjectTreePanel.h"

#include "EntitySelection.h"
#include "Icons.h"
#include "layers/panels/AddSceneObjectMenu.h"
#include "layers/panels/components/context_menus.h"
#include "layers/panels/components/tab_layouts.h"
#include "scenes/Scene.h"
#include "scenes/instances/SceneObject.h"
#include "scenes/instances/scene_components/VisibilityComponent.h"

#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/text_label.h>
#include <components/ui_scope.h>

#include <memory>
#include <string>
#include <unordered_set>

#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

static bool s_showsInOutliner(const Rapture::SceneObject *object)
{
    Rapture::VisibilityComponent *visibility = object->component<Rapture::VisibilityComponent>();
    return visibility == nullptr || visibility->inOutliner;
}

static constexpr float HEADER_HEIGHT = 26.0f;
static constexpr float HEADER_PAD = 6.0f;
static constexpr float CONTENT_OFFSET = HEADER_HEIGHT + (HEADER_PAD * 2.0f);
static constexpr float ADD_BUTTON_WIDTH = 64.0f;

SceneObjectTreePanel::SceneObjectTreePanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, Rapture::SceneObject *root)
    : Panel("Scene Objects", context), m_scene(context.scene), m_rootObject(root)
{
    auto rootFrame = std::make_unique<Amethyst::Frame>();
    m_root = rootFrame.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        m_treeView = nullptr;
        m_contextMenu = nullptr;
    });
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    Amethyst::UIScope scope(*m_root);
    setupHeader(scope);
    setupTree(scope);

    m_contextMenu = m_root->add<Amethyst::ContextMenu>();
    m_contextMenu->setContextMenuProperties({.itemHoverBackground = COL_MENU_HOVER});
    m_contextMenu->setRowFactories({.separator = [] { return std::make_unique<ViewportContextMenuSIV>(); }});

    if (m_scene != nullptr) {
        // deferred, the signal reaches here from inside a context menu callback
        m_hierarchyChangedConn = m_scene->onHierarchyChanged.connect([this]() { m_pendingRefresh = true; });
    }

    if (m_selection != nullptr) {
        m_selectionChangedConn = m_selection->onChanged.connect([this](Rapture::ecs::EntityAccessor entity) { selectRowFor(entity); });
    }

    icon = Icons::SVG_SCENE;
    attach(tabBar, std::move(rootFrame));

    refresh();
}

SceneObjectTreePanel::~SceneObjectTreePanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void SceneObjectTreePanel::setupHeader(Amethyst::UIScope &scope)
{
    scope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(HEADER_PAD, HEADER_PAD),
                    .size = Amethyst::UDim2(1.0f, -HEADER_PAD * 2.0f, 0.0f, HEADER_HEIGHT),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &header) {
            header.textButton({.classes = {"generic-text-button"},
                               .base = {.size = Amethyst::UDim2(0.0f, ADD_BUTTON_WIDTH, 1.0f, 0.0f)},
                               .label = "Add"},
                              [this](Amethyst::TextButtonScope &b) {
                                  b.component.onMouseButton1ClickCb = [this, button = &b.component]() {
                                      showAddMenu(
                                          {button->absolutePosition.x, button->absolutePosition.y + button->absoluteSize.y});
                                      return Amethyst::EventResult::CONSUMED;
                                  };
                              });
        });
}

void SceneObjectTreePanel::setupTree(Amethyst::UIScope &scope)
{
    scope.scrollingFrame(
        {
            .classes = {"panel"},
            .base =
                {
                    .clipsDescendants = true,
                    .position = Amethyst::UDim2::fromOffset(0.0f, CONTENT_OFFSET),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -CONTENT_OFFSET),
                },
            .scroll =
                {
                    .scrollAxis = Amethyst::ScrollAxis::Y,
                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                    .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                },
        },
        [this](Amethyst::ScrollingFrameScope &sf) {
            sf.treeView(
                {
                    .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                    .treeView = {.cellPadding = Amethyst::UDim4{{0.0f, 0.0f}},
                                 .showColumnSeparators = false,
                                 .disclosureTriangleSize = 24.0f,
                                 .showHeader = false},
                },
                [this](Amethyst::TreeViewScope &tv) {
                    m_treeView = &tv.component;
                    tv.column({.header = "Name", .weight = 1.0f, .labelPadding = Amethyst::UDim4{.left = {0.0f, 0.0f}}});
                });
        });

    m_treeView->onRowClicked = [this](uint32_t row) { onRowClicked(row); };
    m_treeView->onRowRightClicked = [this](uint32_t row, Amethyst::vec2 pos) { onRowRightClicked(row, pos); };
}

void SceneObjectTreePanel::onUpdate(float dt)
{
    (void)dt;

    if (m_pendingDeleteObject != nullptr) {
        applyPendingDelete();
    }

    if (m_pendingRefresh) {
        m_pendingRefresh = false;
        refresh();
    }
}

void SceneObjectTreePanel::refresh()
{
    if (m_treeView == nullptr || m_rootObject == nullptr) {
        return;
    }

    // a rebuilt row starts expanded, so what the user collapsed is carried across by scene object
    std::unordered_set<const Rapture::SceneObject *> collapsed;
    for (uint32_t row = 0; row < m_rowObjects.size(); row++) {
        if (!m_treeView->isExpanded(row)) {
            collapsed.insert(m_rowObjects[row]);
        }
    }

    m_treeView->clear();
    m_rowObjects.clear();

    Amethyst::TreeViewScope tvScope(*m_treeView);
    tvScope.columnsExplicit = true;
    tvScope.row([this](Amethyst::TreeRowScope &row) { buildSubtree(m_rootObject, row); });

    for (uint32_t row = 0; row < m_rowObjects.size(); row++) {
        if (collapsed.contains(m_rowObjects[row])) {
            m_treeView->collapse(row);
        }
    }

    if (m_selection != nullptr) {
        selectRowFor(m_selection->entity());
    }
}

void SceneObjectTreePanel::buildSubtree(Rapture::SceneObject *instance, Amethyst::TreeRowScope &rowScope)
{
    if (instance == nullptr) {
        return;
    }

    m_rowObjects.push_back(instance);

    std::string name(instance->name());
    rowScope.cell([name](Amethyst::UIScope &s) {
        s.textLabel(
            {
                .classes = {"treeview-primary-column"},
                .base = {.padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(2.0f)},
                         .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                .style = {.backgroundTransparency = 1.0f},
                .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
                .label = name,
            },
            [](Amethyst::TextLabelScope &l) { l.component.propagate(Amethyst::INTERACTION_CATEGORY_ALL); });
    });

    for (const auto &child : instance->children()) {
        Rapture::SceneObject *childObject = child.get();
        if (!s_showsInOutliner(childObject)) {
            continue;
        }
        rowScope.row([this, childObject](Amethyst::TreeRowScope &childRow) { buildSubtree(childObject, childRow); });
    }
}

Rapture::SceneObject *SceneObjectTreePanel::instanceForRow(uint32_t row) const
{
    if (row >= m_rowObjects.size()) {
        return nullptr;
    }
    return m_rowObjects[row];
}

Rapture::SceneObject *SceneObjectTreePanel::addTarget() const
{
    if (m_treeView != nullptr && m_treeView->selectedRow != Amethyst::TreeView::NO_ROW_SELECTION) {
        if (Rapture::SceneObject *selected = instanceForRow(static_cast<uint32_t>(m_treeView->selectedRow))) {
            return selected;
        }
    }
    return m_rootObject;
}

void SceneObjectTreePanel::showAddMenu(Amethyst::vec2 pos)
{
    if (m_contextMenu == nullptr || m_selection == nullptr) {
        return;
    }

    m_contextMenu->setItems(AddSceneObjectMenu_buildItems(addTarget(), *m_selection, SCENE_OBJECT_SCOPE_ASSET));
    m_contextMenu->showAt(pos);
}

void SceneObjectTreePanel::onRowClicked(uint32_t row)
{
    Rapture::SceneObject *instance = instanceForRow(row);
    if (instance != nullptr && m_selection != nullptr) {
        m_selection->select(instance->accessor());
    }
}

void SceneObjectTreePanel::onRowRightClicked(uint32_t row, Amethyst::vec2 pos)
{
    Rapture::SceneObject *instance = instanceForRow(row);
    if (instance == nullptr || m_contextMenu == nullptr || m_selection == nullptr) {
        return;
    }

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items =
        AddSceneObjectMenu_buildItems(instance, *m_selection, SCENE_OBJECT_SCOPE_ASSET);

    // the root is what the asset is authored into, so it stays
    if (instance != m_rootObject) {
        items.push_back(ViewportContextMenuSID::create());
        items.push_back(Amethyst::makeActionItem("Delete", [this, instance]() { requestDelete(instance); }));
    }

    m_contextMenu->setItems(std::move(items));
    m_contextMenu->showAt(pos);
}

void SceneObjectTreePanel::requestDelete(Rapture::SceneObject *instance)
{
    m_pendingDeleteObject = instance;
}

void SceneObjectTreePanel::applyPendingDelete()
{
    Rapture::SceneObject *instance = m_pendingDeleteObject;
    m_pendingDeleteObject = nullptr;

    if (m_scene == nullptr || instance == nullptr || instance == m_rootObject) {
        return;
    }

    if (m_selection != nullptr && m_selection->entity().getEntity() == instance->entity()) {
        m_selection->clear();
    }

    m_scene->destroyInstance(instance);

    m_treeView->selectedRow = Amethyst::TreeView::NO_ROW_SELECTION;
    refresh();
}

void SceneObjectTreePanel::selectRowFor(Rapture::ecs::EntityAccessor entity)
{
    if (m_treeView == nullptr) {
        return;
    }

    int32_t found = Amethyst::TreeView::NO_ROW_SELECTION;
    if (entity.isValid()) {
        for (uint32_t row = 0; row < m_rowObjects.size(); row++) {
            if (m_rowObjects[row]->entity() == entity.getEntity()) {
                found = static_cast<int32_t>(row);
                break;
            }
        }
    }

    if (m_treeView->selectedRow == found) {
        return;
    }

    m_treeView->selectedRow = found;
    m_treeView->markDirty();
}
