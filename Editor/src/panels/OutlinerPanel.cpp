#include "OutlinerPanel.h"
#include "EntitySelection.h"
#include "Icons.h"
#include "panels/AddSceneObjectMenu.h"
#include "scene/EditorCamera.h"
#include "ui/AssetVisuals.h"
#include "ui/controls/IconLabelButton.h"
#include "ui/controls/SearchField.h"
#include "ui/menus/ContextMenuItems.h"
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/scene/World.h>
#include <rapture/scene/instances/Mesh3D.h>
#include <rapture/scene/instances/Module.h>
#include <rapture/scene/instances/Node3D.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/SkeletonPose.h>

#include <components/common.h>
#include <components/container.h>
#include <components/extensions/ui_flex_item.h>
#include <components/extensions/ui_list_layout.h>
#include <components/context_menu_item.h>
#include <components/text_input.h>
#include <components/text_label.h>
#include <components/ui_scope.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <unordered_set>

#define COL_MENU_HOVER Amethyst::Color3::fromHex(0x4772b3)

static constexpr float ROW_ICON_SIZE = 13.0f;
static constexpr float ROW_ICON_PAD = 2.0f;
static constexpr float ROW_ICON_GAP = 5.0f;
static constexpr float LABEL_TEXT_LEFT = 2.0f;
static constexpr float DISCLOSURE_TRIANGLE_SIZE = 24.0f;
static constexpr float DISCLOSURE_TRIANGLE_PADDING = 4.0f;
static constexpr float NAME_TEXT_LEFT =
    DISCLOSURE_TRIANGLE_SIZE + DISCLOSURE_TRIANGLE_PADDING * 2.0f + ROW_ICON_PAD + ROW_ICON_SIZE + ROW_ICON_GAP;
static constexpr float SEARCH_FIELD_MARGIN = 8.0f;
static constexpr float SEARCH_FIELD_HEIGHT = 26.0f;
static constexpr float SEARCH_TREE_GAP = 6.0f;
static constexpr float SEARCH_BAR_GAP = 6.0f;
static constexpr std::string_view ADD_BUTTON_CLASS = "generic-text-button";
static constexpr float TREE_TOP = SEARCH_FIELD_MARGIN + SEARCH_FIELD_HEIGHT + SEARCH_TREE_GAP;

static bool s_showsInOutliner(const Rapture::SceneObject *object)
{
    return object->visibility().inOutliner;
}

static Rapture::AssetHandle s_assetWithFileBehindObject(const Rapture::SceneObject &object)
{
    Rapture::AssetHandle handle = Rapture::INVALID_ASSET_HANDLE;
    if (const auto *module = object.as<Rapture::Module>(); module != nullptr) {
        handle = module->assetHandle();
    } else if (const auto *mesh = object.as<Rapture::Mesh3D>(); mesh != nullptr) {
        handle = mesh->mesh();
    } else if (const auto *pose = object.as<Rapture::SkeletonPose>(); pose != nullptr) {
        handle = pose->skeleton();
    }

    if (handle == Rapture::INVALID_ASSET_HANDLE || Rapture::AssetManager::getAssetMetadata(handle).assetPath.empty()) {
        return Rapture::INVALID_ASSET_HANDLE;
    }
    return handle;
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

static float s_addRowIconReturningTextLeft(Amethyst::UIScope &s, const SceneObjectIcon &icon)
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
    return ROW_ICON_PAD + ROW_ICON_SIZE + ROW_ICON_GAP;
}

static void s_nameLabel(Amethyst::UIScope &s, const std::string &text, std::string_view className,
                        const SceneObjectIcon *icon = nullptr)
{
    float textLeft = LABEL_TEXT_LEFT;

    if (icon != nullptr) {
        textLeft = s_addRowIconReturningTextLeft(s, *icon);
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

OutlinerPanel::OutlinerPanel(const WorkspaceContext &context) : Panel("Outliner", context)
{
    m_icon = Icons::SVG_LAYERS;
}

void OutlinerPanel::setupSearchBar(Amethyst::UIScope &scope)
{
    scope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(SEARCH_FIELD_MARGIN, SEARCH_FIELD_MARGIN),
                    .size = Amethyst::UDim2(1.0f, -2.0f * SEARCH_FIELD_MARGIN, 0.0f, SEARCH_FIELD_HEIGHT),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &bar) {
            auto *row = bar.component.addExtension<Amethyst::UIListLayout>();
            row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
            row->horizontalFlex = Amethyst::UiFlexAlignment::FILL;
            row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
            row->innerPadding = Amethyst::UDim::fromOffset(SEARCH_BAR_GAP);

            SearchField *searchField = bar.component.add<SearchField>("Search");
            searchField->setBaseProperties({.layoutOrder = 0, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)});
            searchField->addExtension<Amethyst::UIFlexItem>();
            searchField->setOnTextChanged([this](const std::string &text) {
                m_loweredFilter = s_toLowercase(text);
                m_pendingRefresh = true;
            });

            IconLabelButton *addButton =
                bar.component.add<IconLabelButton>(Icons::SVG_PLUS, "Add", std::vector<std::string>{std::string(ADD_BUTTON_CLASS)});
            addButton->setBaseProperties({.layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)});
            addButton->track(addButton->onMouseButton1ClickCb.connect([this, addButton]() {
                showAddMenu({addButton->absolutePosition.x, addButton->absolutePosition.y + addButton->absoluteSize.y});
            }));
        });
}

void OutlinerPanel::showAddMenu(Amethyst::vec2 pos)
{
    if (m_scene == nullptr || m_selection == nullptr) {
        return;
    }

    showContextMenu(pos, AddSceneObjectMenu_buildItems(m_scene->root(), *m_selection, SCENE_OBJECT_SCOPE_LEVEL));
}

void OutlinerPanel::build(Amethyst::UIScope &scope)
{
    setupSearchBar(scope);

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
            tv.column("", 0.5f);
        });

    m_treeView->onRowClicked = [this](uint32_t row) { onRowClicked(row); };
    m_treeView->onRowRightClicked = [this](uint32_t row, Amethyst::vec2 pos) { onRowRightClicked(row, pos); };
    m_treeView->onEmptySpaceRightClicked = [this](Amethyst::vec2 pos) { showAddMenu(pos); };

    m_contextMenu = m_root->add<Amethyst::ContextMenu>();
    m_contextMenu->setContextMenuProperties({.itemHoverBackground = COL_MENU_HOVER});
    m_contextMenu->setRowFactories({.separator = [] { return std::make_unique<SeparatorMenuItemView>(); }});

    if (m_context.editor->shortcuts != nullptr) {
        m_context.editor->shortcuts->registerShortcut(EDITOR_COMMAND_FOCUS_SELECTION, {Rapture::KEY_F}, m_root,
                                                      [this]() { focusOnSelection(); });
    }

    setScene(m_context.scene);
}

void OutlinerPanel::focusOnSelection()
{
    if (m_context.editorCamera == nullptr || m_scene == nullptr || m_selection == nullptr || !m_selection->has()) {
        return;
    }

    Rapture::SceneObject *object = m_scene->instanceFor(m_selection->entity().getEntity());
    if (object != nullptr) {
        m_context.editorCamera->focusOnObject(*object);
    }
}

void OutlinerPanel::onRootDestroyed()
{
    m_treeView = nullptr;
    m_contextMenu = nullptr;
    m_renameInput = nullptr;
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

    // a rebuilt row starts expanded, so what the user collapsed is carried across by instance; a filtered
    // tree shows every match expanded, so it leaves the collapsed set from before the search alone
    if (!m_builtWithFilter) {
        m_collapsedInstances.clear();
        for (uint32_t row = 0; row < m_rowInstances.size(); row++) {
            if (!m_treeView->isExpanded(row)) {
                m_collapsedInstances.insert(m_rowInstances[row]);
            }
        }
    }
    m_builtWithFilter = !m_loweredFilter.empty();

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
        if (!s_showsInOutliner(instance) || !s_subtreeMatchesFilter(instance, m_loweredFilter)) {
            continue;
        }
        tvScope.row([this, instance](Amethyst::TreeRowScope &row) { buildInstanceTree(instance, row); });
    }

    if (m_builtWithFilter) {
        return;
    }
    for (uint32_t row = 0; row < m_rowInstances.size(); row++) {
        if (m_collapsedInstances.contains(m_rowInstances[row])) {
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

    // a placed module reads as the asset it stands for, the way a class reads as itself
    if (const Rapture::Module *placedModule = instance->as<Rapture::Module>()) {
        const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(placedModule->assetHandle());
        if (metadata) {
            typeName = metadata.getName();
        }
    }

    SceneObjectIcon icon = SceneObject_iconForClass(&instance->type());
    rowScope.cell([instanceName, icon](Amethyst::UIScope &s) { s_nameLabel(s, instanceName, "treeview-primary-column", &icon); });
    rowScope.cell([typeName](Amethyst::UIScope &s) { s_nameLabel(s, typeName, "treeview-secondary-column"); });

    for (const auto &child : instance->children()) {
        Rapture::SceneObject *childInstance = child.get();
        if (!s_showsInOutliner(childInstance) || !s_subtreeMatchesFilter(childInstance, m_loweredFilter)) {
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
    items.push_back(Amethyst::makeActionItem("Rename", [this, row, instance]() { startRename(row, instance); }));

    if (m_context.editorCamera != nullptr && instance->isA<Rapture::Node3D>()) {
        items.push_back(Amethyst::makeActionItem("Focus in Viewport", [this, instance]() { m_context.editorCamera->focusOnObject(*instance); }));
    }

    const Rapture::AssetHandle asset = s_assetWithFileBehindObject(*instance);
    if (asset != Rapture::INVALID_ASSET_HANDLE && m_context.editor->showAssetInContentBrowser != nullptr) {
        items.push_back(Amethyst::makeActionItem("Show in Content Browser",
                                                 [this, asset]() { m_context.editor->showAssetInContentBrowser(asset); }));
    }

    items.push_back(SeparatorMenuItem::create());
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
            if (!child->setParent(parent)) {
                break;
            }
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

    const SceneObjectIcon icon = SceneObject_iconForClass(&instance->type());
    auto cell = std::make_unique<Amethyst::Container>();
    cell->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
    Amethyst::UIScope scope(*cell);

    if (!editing) {
        s_nameLabel(scope, name, "treeview-primary-column", &icon);
        m_treeView->setCell(row, 0, std::move(cell));
        return;
    }

    const float textLeft = s_addRowIconReturningTextLeft(scope, icon);

    auto *input = cell->add<Amethyst::TextInput>();
    m_renameInput = input;
    input->addClass("property-input-field");
    input->setText(name);
    input->setBaseProperties({
        .padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(2.0f)},
        .position = Amethyst::UDim2::fromOffset(textLeft, 0.0f),
        .size = Amethyst::UDim2(1.0f, -textLeft, 1.0f, 0.0f),
        .zIndex = 2,
    });
    input->setTextInputProperties({.text = {.fontSize = 14.0f, .textYAlignment = Amethyst::TextYAlignment::CENTER}});
    input->onEnterPressed = [this, instance, input]() { onRenameCommitted(instance, input->getText()); };
    input->onFocusLost = [this, instance, input]() { onRenameCommitted(instance, input->getText()); };

    m_treeView->setCell(row, 0, std::move(cell));

    input->focus();
    input->selectAll();
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
