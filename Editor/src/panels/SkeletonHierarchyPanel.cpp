#include "SkeletonHierarchyPanel.h"

#include "EntitySelection.h"
#include "Icons.h"
#include "layers/panels/components/asset_visuals.h"
#include "scene/instances/SceneObject.h"
#include "scene/instances/SkeletonPose.h"

#include <components/common.h>
#include <components/text_label.h>
#include <components/ui_scope.h>

#include <memory>
#include <string>
#include <unordered_set>

static constexpr float ROW_ICON_SIZE = 13.0f;
static constexpr float ROW_ICON_PAD = 2.0f;
static constexpr float ROW_ICON_GAP = 5.0f;

static void s_nameCell(Amethyst::UIScope &s, const std::string &text, const SceneObjectIcon &icon)
{
    s.imageLabel({
        .classes = {std::string(icon.styleClass)},
        .base = {.anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                 .interactable = false,
                 .position = Amethyst::UDim2(0.0f, ROW_ICON_PAD, 0.5f, 0.0f),
                 .size = Amethyst::UDim2::fromOffset(ROW_ICON_SIZE, ROW_ICON_SIZE)},
        .style = {.backgroundTransparency = 1.0f},
        .svg = icon.svg,
    });

    Amethyst::UDim4 padding = {.left = Amethyst::UDim::fromOffset(ROW_ICON_PAD + ROW_ICON_SIZE + ROW_ICON_GAP)};
    s.textLabel(
        {
            .classes = {"treeview-primary-column"},
            .base = {.padding = padding, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
            .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
            .label = text,
        },
        [](Amethyst::TextLabelScope &l) { l.component.propagate(Amethyst::INTERACTION_CATEGORY_ALL); });
}

SkeletonHierarchyPanel::SkeletonHierarchyPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context,
                                               Rapture::SkeletonPose *pose)
    : Panel("Hierarchy", context), m_pose(pose)
{
    auto rootFrame = std::make_unique<Amethyst::Frame>();
    m_root = rootFrame.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        m_treeView = nullptr;
    });
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    Amethyst::UIScope scope(*m_root);
    setupTreeView(scope);

    if (m_selection != nullptr) {
        m_selectionChangedConn =
            m_selection->onChanged.connect([this](Rapture::ecs::EntityAccessor entity) { selectRowFor(entity); });
    }

    icon = Icons::SVG_BONE;
    attach(tabBar, std::move(rootFrame));

    refresh();
}

SkeletonHierarchyPanel::~SkeletonHierarchyPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void SkeletonHierarchyPanel::setupTreeView(Amethyst::UIScope &scope)
{
    scope.scrollingFrame(
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
}

void SkeletonHierarchyPanel::refresh()
{
    if (m_treeView == nullptr || m_pose == nullptr) {
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
    tvScope.row([this](Amethyst::TreeRowScope &row) { buildRows(m_pose, row); });

    for (uint32_t row = 0; row < m_rowObjects.size(); row++) {
        if (collapsed.contains(m_rowObjects[row])) {
            m_treeView->collapse(row);
        }
    }

    if (m_selection != nullptr) {
        selectRowFor(m_selection->entity());
    }
}

void SkeletonHierarchyPanel::buildRows(Rapture::SceneObject *object, Amethyst::TreeRowScope &rowScope)
{
    m_rowObjects.push_back(object);

    std::string name(object->name());
    SceneObjectIcon icon = SceneObject_iconForClass(&object->type());
    rowScope.cell([name, icon](Amethyst::UIScope &s) { s_nameCell(s, name, icon); });

    for (const auto &child : object->children(true)) {
        Rapture::SceneObject *childObject = child.get();
        rowScope.row([this, childObject](Amethyst::TreeRowScope &childRow) { buildRows(childObject, childRow); });
    }
}

Rapture::SceneObject *SkeletonHierarchyPanel::objectForRow(uint32_t row) const
{
    if (row >= m_rowObjects.size()) {
        return nullptr;
    }
    return m_rowObjects[row];
}

void SkeletonHierarchyPanel::onRowClicked(uint32_t row)
{
    Rapture::SceneObject *object = objectForRow(row);
    if (object != nullptr && m_selection != nullptr) {
        m_selection->select(object->accessor());
    }
}

void SkeletonHierarchyPanel::selectRowFor(Rapture::ecs::EntityAccessor entity)
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
