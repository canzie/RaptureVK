#include "SceneObjectTreePanel.h"

#include "EntitySelection.h"
#include "Icons.h"
#include "panels/AddSceneObjectMenu.h"
#include "ui/AssetVisuals.h"
#include "ui/controls/SearchField.h"
#include "ui/menus/ContextMenuItems.h"
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/SceneObject.h>

#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/extensions/ui_list_layout.h>
#include <components/text_label.h>
#include <components/ui_scope.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <unordered_set>

#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

static bool s_showsInOutliner(const Rapture::SceneObject *object)
{
    return object->visibility().inOutliner;
}

static std::string s_toLowercase(std::string_view text)
{
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

static bool s_subtreeMatchesFilter(const Rapture::SceneObject *object, std::string_view loweredFilter)
{
    if (loweredFilter.empty() || s_toLowercase(object->name()).find(loweredFilter) != std::string::npos) {
        return true;
    }
    for (const auto &child : object->children()) {
        if (s_showsInOutliner(child.get()) && s_subtreeMatchesFilter(child.get(), loweredFilter)) {
            return true;
        }
    }
    return false;
}

static constexpr float SEARCH_FIELD_MARGIN = 8.0f;
static constexpr float SEARCH_FIELD_HEIGHT = 26.0f;
static constexpr float SEARCH_TREE_GAP = 6.0f;
static constexpr float TREE_TOP = SEARCH_FIELD_MARGIN + SEARCH_FIELD_HEIGHT + SEARCH_TREE_GAP;

static constexpr float DISCLOSURE_TRIANGLE_SIZE = 24.0f;
static constexpr float DISCLOSURE_TRIANGLE_PADDING = 4.0f;
static constexpr float ROW_ICON_SIZE = 13.0f;
static constexpr float ROW_ICON_PAD = 2.0f;
static constexpr float ROW_ITEM_GAP = 5.0f;
static constexpr float COMPONENT_ICON_SIZE = 12.0f;
static constexpr float COMPONENTS_GAP = 4.0f;
static constexpr float ROOT_CHIP_HEIGHT = 16.0f;
static constexpr float ROOT_CHIP_PADDING = 6.0f;
static constexpr float LABEL_TEXT_LEFT = 2.0f;
static constexpr float NAME_TEXT_LEFT =
    DISCLOSURE_TRIANGLE_SIZE + DISCLOSURE_TRIANGLE_PADDING * 2.0f + ROW_ICON_PAD + ROW_ICON_SIZE + ROW_ITEM_GAP;

static constexpr std::string_view PRIMARY_COLUMN_CLASS = "treeview-primary-column";
static constexpr std::string_view SECONDARY_COLUMN_CLASS = "treeview-secondary-column";
static constexpr std::string_view ROOT_CHIP_CLASS = "scene-object-root-chip";
static constexpr std::string_view COMPONENT_ICON_CLASS = "scene-object-component-icon";

SceneObjectTreePanel::SceneObjectTreePanel(const WorkspaceContext &context, Rapture::SceneObject *root)
    : Panel("Scene Objects", context), m_scene(context.scene), m_rootObject(root)
{
    m_icon = Icons::SVG_LAYERS;
}

void SceneObjectTreePanel::build(Amethyst::UIScope &scope)
{
    setupSearchField(scope);
    setupTree(scope);

    m_contextMenu = m_root->add<Amethyst::ContextMenu>();
    m_contextMenu->setContextMenuProperties({.itemHoverBackground = COL_MENU_HOVER});
    m_contextMenu->setRowFactories({.separator = [] { return std::make_unique<SeparatorMenuItemView>(); }});

    if (m_scene != nullptr) {
        // deferred, the signal reaches here from inside a context menu callback
        m_hierarchyChangedConn = m_scene->onHierarchyChanged.connect([this]() { m_pendingRefresh = true; });
    }

    if (m_selection != nullptr) {
        m_selectionChangedConn =
            m_selection->onChanged.connect([this](Rapture::ecs::EntityAccessor entity) { selectRowFor(entity); });
    }

    refresh();
}

void SceneObjectTreePanel::onRootDestroyed()
{
    m_treeView = nullptr;
    m_contextMenu = nullptr;
}

void SceneObjectTreePanel::setupSearchField(Amethyst::UIScope &scope)
{
    SearchField *searchField = scope.get().add<SearchField>("Search");
    searchField->setBaseProperties({
        .position = Amethyst::UDim2::fromOffset(SEARCH_FIELD_MARGIN, SEARCH_FIELD_MARGIN),
        .size = Amethyst::UDim2(1.0f, -2.0f * SEARCH_FIELD_MARGIN, 0.0f, SEARCH_FIELD_HEIGHT),
    });
    searchField->setOnTextChanged([this](const std::string &text) {
        m_loweredFilter = s_toLowercase(text);
        m_pendingRefresh = true;
    });
}

void SceneObjectTreePanel::setupTree(Amethyst::UIScope &scope)
{
    scope.treeView(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, TREE_TOP),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -TREE_TOP),
                },
            .treeView = {.cellPadding = Amethyst::UDim4{{0.0f, 0.0f}},
                         .showColumnSeparators = false,
                         .disclosureTriangleSize = DISCLOSURE_TRIANGLE_SIZE,
                         .disclosureTrianglePadding = DISCLOSURE_TRIANGLE_PADDING,
                         .fillRows = true,
                         .showHeader = true},
        },
        [this](Amethyst::TreeViewScope &tv) {
            m_treeView = &tv.component;
            tv.column({.header = "Name", .weight = 3.0f, .labelPadding = Amethyst::UDim4{.left = {0.0f, NAME_TEXT_LEFT}}});
            tv.column({.header = "Type", .weight = 1.0f, .labelPadding = Amethyst::UDim4{.left = {0.0f, LABEL_TEXT_LEFT}}});
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

    rowScope.cell([this, instance](Amethyst::UIScope &cell) { buildNameCell(cell, instance); });

    std::string typeName(instance->type().name);
    rowScope.cell([typeName](Amethyst::UIScope &cell) {
        cell.textLabel(
            {
                .classes = {std::string(SECONDARY_COLUMN_CLASS)},
                .base = {.padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(LABEL_TEXT_LEFT)},
                         .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                .style = {.backgroundTransparency = 1.0f},
                .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
                .label = typeName,
            },
            [](Amethyst::TextLabelScope &l) { l.component.propagate(Amethyst::INTERACTION_CATEGORY_ALL); });
    });

    for (const auto &child : instance->children()) {
        Rapture::SceneObject *childObject = child.get();
        if (!s_showsInOutliner(childObject) || !s_subtreeMatchesFilter(childObject, m_loweredFilter)) {
            continue;
        }
        rowScope.row([this, childObject](Amethyst::TreeRowScope &childRow) { buildSubtree(childObject, childRow); });
    }
}

void SceneObjectTreePanel::buildNameCell(Amethyst::UIScope &cell, const Rapture::SceneObject *instance) const
{
    const SceneObjectIcon icon = SceneObject_iconForClass(&instance->type());
    const bool isRoot = instance == m_rootObject;

    cell.frame(
        {
            .base = {.padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(ROW_ICON_PAD)},
                     .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
        },
        [&](Amethyst::FrameScope &row) {
            row.component.propagate(Amethyst::INTERACTION_CATEGORY_ALL);
            auto *layout = row.component.addExtension<Amethyst::UIListLayout>();
            layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
            layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
            layout->innerPadding = Amethyst::UDim::fromOffset(ROW_ITEM_GAP);

            uint32_t order = 0;
            row.imageLabel({
                .classes = {std::string(icon.styleClass)},
                .base = {.interactable = false, .layoutOrder = order++, .size = Amethyst::UDim2::fromOffset(ROW_ICON_SIZE, ROW_ICON_SIZE)},
                .style = {.backgroundTransparency = 1.0f},
                .svg = icon.svg,
            });

            row.textLabel(
                {
                    .classes = {std::string(PRIMARY_COLUMN_CLASS)},
                    .base = {.automaticSize = Amethyst::AutomaticSize::X, .layoutOrder = order++, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                    .style = {.backgroundTransparency = 1.0f},
                    .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
                    .label = std::string(instance->name()),
                },
                [](Amethyst::TextLabelScope &l) { l.component.propagate(Amethyst::INTERACTION_CATEGORY_ALL); });

            if (isRoot) {
                row.textLabel(
                    {
                        .classes = {std::string(ROOT_CHIP_CLASS)},
                        .base = {.automaticSize = Amethyst::AutomaticSize::X,
                                 .layoutOrder = order++,
                                 .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(ROOT_CHIP_PADDING),
                                                            .left = Amethyst::UDim::fromOffset(ROOT_CHIP_PADDING)},
                                 .size = Amethyst::UDim2::fromOffset(0.0f, ROOT_CHIP_HEIGHT)},
                        .style = {.backgroundColor = Asset_colorForType(Rapture::ASSET_MODULE)},
                        .label = "Root",
                    },
                    [](Amethyst::TextLabelScope &l) { l.component.propagate(Amethyst::INTERACTION_CATEGORY_ALL); });
            }

            row.frame(
                {
                    .base = {.automaticSize = Amethyst::AutomaticSize::X,
                             .interactable = false,
                             .layoutOrder = order++,
                             .size = Amethyst::UDim2::fromOffset(0.0f, COMPONENT_ICON_SIZE)},
                    .style = {.backgroundTransparency = 1.0f},
                },
                [&](Amethyst::FrameScope &components) {
                    auto *iconRow = components.component.addExtension<Amethyst::UIListLayout>();
                    iconRow->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    iconRow->innerPadding = Amethyst::UDim::fromOffset(COMPONENTS_GAP);

                    uint32_t componentOrder = 0;
                    for (const auto &component : instance->components()) {
                        const SceneComponentDescription *description = SceneComponent_describeClass(component->type().name);
                        if (description == nullptr) {
                            continue;
                        }
                        components.imageLabel({
                            .classes = {std::string(COMPONENT_ICON_CLASS)},
                            .base = {.interactable = false,
                                     .layoutOrder = componentOrder++,
                                     .size = Amethyst::UDim2::fromOffset(COMPONENT_ICON_SIZE, COMPONENT_ICON_SIZE)},
                            .svg = description->svg,
                        });
                    }
                });
        });
}

Rapture::SceneObject *SceneObjectTreePanel::instanceForRow(uint32_t row) const
{
    if (row >= m_rowObjects.size()) {
        return nullptr;
    }
    return m_rowObjects[row];
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
        items.push_back(SeparatorMenuItem::create());
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
