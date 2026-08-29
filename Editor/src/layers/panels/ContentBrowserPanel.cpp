#include "ContentBrowserPanel.h"
#include "Icons.h"
#include "assets/asset_manager/AssetManager.h"
#include "scene/instances/SceneObject.h"
#include "layers/panels/components/asset_visuals.h"
#include "layers/panels/components/context_menus.h"
#include "core/utils/Log.h"
#include "core/utils/StringFormat.h"
#include "scene/Project.h"
#include "scene/instances/Camera3D.h"
#include "scene/instances/DirectionalLight3D.h"
#include "scene/instances/InstanceRegistry.h"
#include "scene/instances/Module.h"
#include "scene/instances/PointLight3D.h"
#include "scene/instances/SpotLight3D.h"
#include "scene/instances/SpringArm3D.h"
#include "scene/instances/StaticMesh3D.h"
#include "scene/instances/controllers/CameraController.h"
#include "scene/instances/controllers/PlayerController.h"
#include "scene/World.h"
#include "core/ecs/entity_accessor.h"
#include "app/Application.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/extensions/ui_aspect_ratio_constraint.h>
#include <components/extensions/ui_grid_layout.h>
#include <components/extensions/ui_list_layout.h>
#include <components/frame.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <memory>
#include <modules/color.h>
#include <string_view>

static constexpr float TOP_BAR_HEIGHT = 36.0f;
static constexpr float SIDE_BAR_WIDTH = 200.0f;
static constexpr float SEARCH_BAR_HEIGHT = 36.0f;
static constexpr float STATUS_BAR_HEIGHT = 24.0f;
static constexpr float SECTION_HEADER_HEIGHT = 26.0f;

static constexpr float TILE_MIN_WIDTH = 150.0f;
static constexpr float TILE_MAX_WIDTH = 210.0f;
static constexpr float TILE_ASPECT = 0.75f;
static constexpr float TILE_FOOTER_HEIGHT = 46.0f;
static constexpr float TILE_TYPEBAR_HEIGHT = 3.0f;
static constexpr float TILE_ICON_SIZE = 42.0f;

#define COL_SELECTION Amethyst::Color3(0.13f, 0.45f, 0.85f)
#define COL_SEPARATOR Amethyst::Color3::fromHex(0x181818)
#define COL_ICON      Amethyst::Color4(0.8f, 0.8f, 0.8f, 1.0f)
#define COL_HOVER     Amethyst::Color3::fromHex(0x4d4d4d)

static constexpr float CONTENT_PADDING = 10.0f;
static constexpr float ADD_MENU_WIDTH = 240.0f;

static constexpr float TOOLTIP_WIDTH = 240.0f;
static constexpr float TOOLTIP_ROW_HEIGHT = 18.0f;
static constexpr float TOOLTIP_PADDING = 8.0f;

using MenuItems = std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>>;
using TooltipRows = std::vector<std::pair<std::string, std::string>>;

// TODO: replace with the asset creation panel these rows will open once it exists
static void s_createAssetStub(std::string_view what)
{
    RP_INFO("creating a {} is not wired up yet", what);
}

// the file name an asset takes from its display name
static std::string s_assetFileName(std::string_view name)
{
    std::string fileName(name);
    std::replace(fileName.begin(), fileName.end(), ' ', '_');
    return fileName;
}

// an authored asset is labelled with the class it holds, every other asset with its type
static std::string s_typeLabel(Rapture::AssetType type, const Rapture::TypeInfo *authoredClass)
{
    if (authoredClass != nullptr && type == Rapture::ASSET_MODULE) {
        return std::string(authoredClass->name);
    }
    return Rapture::AssetTypeToString(type);
}

static void s_setActive(Amethyst::UIObject &object, bool active)
{
    uint16_t state = object.getGuiState();
    object.setGuiState(active ? static_cast<uint16_t>(state | Amethyst::GUI_STATE_ACTIVE)
                              : static_cast<uint16_t>(state & ~Amethyst::GUI_STATE_ACTIVE));
}

static std::string s_uniqueFolderName(const std::filesystem::path &directory)
{
    std::string name = "Folder";
    for (uint32_t suffix = 2; std::filesystem::exists(directory / name); suffix++) {
        name = "Folder " + std::to_string(suffix);
    }
    return name;
}

static std::string s_uniqueAssetName(const std::filesystem::path &directory, std::string_view baseName, Rapture::AssetType type)
{
    std::string extension(Rapture::AssetTypeToExtension(type));
    std::string name(baseName);
    for (uint32_t suffix = 2; std::filesystem::exists(directory / (s_assetFileName(name) + extension)); suffix++) {
        name = std::string(baseName) + " " + std::to_string(suffix);
    }
    return name;
}

static void s_spawnModule(Rapture::AssetHandle handle, Rapture::Scene *scene)
{
    if (scene == nullptr) {
        RP_WARN("No scene to spawn into");
        return;
    }

    const Rapture::AssetMetadata &metadata = Rapture::AssetManager::getAssetMetadata(handle);
    Rapture::Module *placedModule = scene->root()->add<Rapture::Module>(metadata.getName());

    if (!placedModule->setAssetHandle(handle)) {
        scene->root()->removeChild(placedModule);
        RP_WARN("Failed to spawn asset {} into the scene", handle);
    }
}

static std::string s_normalizeForSearch(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == ' ') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

static float s_estimateTextWidth(const std::string &text, float fontSize)
{
    return static_cast<float>(text.size()) * fontSize * 0.55f + 14.0f;
}

static void s_fillTooltip(Amethyst::Tooltip &surface, const TooltipRows &rows)
{
    surface.removeAllChildren();
    surface.setClasses({"tooltip"});
    surface.setBaseProperties({
        .padding = Amethyst::UDim4::fromOffset(TOOLTIP_PADDING),
        .size = Amethyst::UDim2::fromOffset(TOOLTIP_WIDTH,
                                            2.0f * TOOLTIP_PADDING + static_cast<float>(rows.size()) * TOOLTIP_ROW_HEIGHT),
    });

    auto *layout = surface.addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_TOP;

    Amethyst::UIScope scope(surface);
    uint32_t order = 0;
    for (const auto &[label, value] : rows) {
        scope.textLabel({
            .classes = {"tooltip-row"},
            .base =
                {
                    .interactable = false,
                    .layoutOrder = order++,
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TOOLTIP_ROW_HEIGHT),
                },
            .label = label + ": " + value,
        });
    }
}
ContentBrowserPanel::ContentBrowserPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context)
    : Panel("Content Browser", context)
{
    m_isDocked = true;
    m_scene = context.world != nullptr ? context.world->getScene() : nullptr;

    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();

    buildContent();
    attach(tabBar, std::move(root));
}

ContentBrowserPanel::ContentBrowserPanel(Amethyst::PopupScope &scope, const PanelServices &services)
    : Panel("Content Browser", services)
{
    m_root = &scope.component;
    buildContent();
}

ContentBrowserPanel::~ContentBrowserPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void ContentBrowserPanel::setContext(const WorkspaceContext &context)
{
    Panel::setContext(context);
    m_scene = context.world != nullptr ? context.world->getScene() : nullptr;
}

void ContentBrowserPanel::buildContent()
{
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });

    m_baseDirectory = Rapture::Application::getInstance().getProject().getProjectDirectory();
    m_currentDirectory = m_baseDirectory;
    m_navigationHistory.push_back(m_currentDirectory);

    m_root->setBaseStyleProperties({
        .borderMode = Amethyst::BorderMode::INSET,
        .borderPixelSize = 2.0f,
        .borderColor = Amethyst::Color3::fromHex(0x181818),
    });

    setupTopBar();
    setupSideBar();
    setupContentArea();
    setupContextMenu();

    buildDirectoryTree();
    refresh();
}

void ContentBrowserPanel::setupTopBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .classes = {"content-browser-section"},
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TOP_BAR_HEIGHT),
                },
        },
        [this](Amethyst::FrameScope &top) {
            m_topBarPane = &top.component;

            // Single horizontal list-layout cluster so item positions are computed, not hardcoded.
            // It spans from the left edge up to the settings button reserved on the right.
            top.frame(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .clipsDescendants = true,
                            .position = Amethyst::UDim2(0.0f, CONTENT_PADDING, 0.5f, 0.0f),
                            .size = Amethyst::UDim2(1.0f, -(CONTENT_PADDING + 40.0f), 1.0f, -10.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f},
                },
                [this](Amethyst::FrameScope &cluster) {
                    auto *layout = cluster.component.addExtension<Amethyst::UIListLayout>();
                    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                    layout->innerPadding = Amethyst::UDim::fromOffset(4.0f);

                    cluster.textButton(
                        {
                            .classes = {"generic-text-button"},
                            .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(56.0f, 24.0f)},
                            .label = "+ Add",
                        },
                        [this](Amethyst::TextButtonScope &b) {
                            m_addBtn = &b.component;
                            m_addBtn->onMouseButton1ClickCb = [this]() {
                                showAddMenu({m_addBtn->absolutePosition.x, m_addBtn->absolutePosition.y});
                                return Amethyst::EventResult::CONSUMED;
                            };
                        });
                    cluster.textButton(
                        {
                            .classes = {"generic-text-button"},
                            .base = {.layoutOrder = 1, .size = Amethyst::UDim2::fromOffset(58.0f, 24.0f)},
                            .label = "Import",
                        },
                        [this](Amethyst::TextButtonScope &b) {
                            m_importBtn = &b.component;
                            m_importBtn->onMouseButton1ClickCb = [this]() {
                                m_services.openFileExplorer(FileBrowser::Mode::OPEN, [this](const std::filesystem::path &path) {
                                    m_services.openImportPanel(path, m_currentDirectory);
                                });
                                return Amethyst::EventResult::CONSUMED;
                            };
                        });
                    cluster.imageButton(
                        {
                            .base = {.layoutOrder = 2, .size = Amethyst::UDim2::fromOffset(22.0f, 22.0f)},
                            .style = {.backgroundTransparency = 1.0f, .cornerRadius = 3.0f},
                            .image = {.imageColor = COL_ICON},
                            .svg = Icons::SVG_CARET_SMALL,
                        },
                        [this](Amethyst::ImageButtonScope &b) {
                            m_goBackBtn = &b.component;
                            b.component.setBaseProperties({.rotation = Amethyst::Degrees(90.0f)});
                            b.component.onMouseButton1ClickCb = [this]() {
                                navigateBack();
                                return Amethyst::EventResult::CONSUMED;
                            };
                        });
                    cluster.imageButton(
                        {
                            .base = {.layoutOrder = 3, .size = Amethyst::UDim2::fromOffset(22.0f, 22.0f)},
                            .style = {.backgroundTransparency = 1.0f, .cornerRadius = 3.0f},
                            .image = {.imageColor = COL_ICON},
                            .svg = Icons::SVG_CARET_SMALL,
                        },
                        [this](Amethyst::ImageButtonScope &b) {
                            m_goForwardBtn = &b.component;
                            b.component.setBaseProperties({.rotation = Amethyst::Degrees(-90.0f)});
                            b.component.onMouseButton1ClickCb = [this]() {
                                navigateForward();
                                return Amethyst::EventResult::CONSUMED;
                            };
                        });
                    cluster.imageLabel({
                        .base = {.layoutOrder = 4, .size = Amethyst::UDim2::fromOffset(16.0f, 16.0f)},
                        .style = {.backgroundTransparency = 1.0f},
                        .image = {.imageColor = Amethyst::Color4(0.85f, 0.72f, 0.4f, 1.0f)},
                        .svg = Icons::SVG_FOLDER,
                    });
                    // Breadcrumb is the last item; it consumes the remaining width and clips its overflow.
                    cluster.frame(
                        {
                            .base =
                                {
                                    .clipsDescendants = true,
                                    .layoutOrder = 5,
                                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, 0.0f),
                                },
                            .style = {.backgroundTransparency = 1.0f},
                        },
                        [this](Amethyst::FrameScope &crumbs) {
                            m_breadcrumbBar = &crumbs.component;
                            auto *crumbLayout = crumbs.component.addExtension<Amethyst::UIListLayout>();
                            crumbLayout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                            crumbLayout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                            crumbLayout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                            crumbLayout->innerPadding = Amethyst::UDim::fromOffset(2.0f);
                        });
                });

            top.imageButton(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                            .position = Amethyst::UDim2(1.0f, -8.0f, 0.5f, 0.0f),
                            .size = Amethyst::UDim2::fromOffset(22.0f, 22.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f, .cornerRadius = 3.0f},
                    .image = {.imageColor = COL_ICON},
                    .svg = Icons::SVG_SETTINGS,
                },
                [this](Amethyst::ImageButtonScope &b) { m_settingsBtn = &b.component; });

            if (!m_isDocked) {
                top.textButton(
                    {
                        .classes = {"generic-text-button"},
                        .base =
                            {
                                .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                                .position = Amethyst::UDim2(1.0f, -38.0f, 0.5f, 0.0f),
                                .size = Amethyst::UDim2::fromOffset(100.0f, 24.0f),
                            },
                        .label = "Dock in layout",
                    },
                    [this](Amethyst::TextButtonScope &b) {
                        b.component.onMouseButton1ClickCb = [this]() {
                            m_isDocked = true;
                            onDockInLayout.fire();
                            return Amethyst::EventResult::CONSUMED;
                        };
                    });
            }
        });
}

static Amethyst::CollapsibleHeaderStylePropertiesArgs s_sidebarHeaderStyle()
{
    return {
        .headerHeight = SECTION_HEADER_HEIGHT,
        .indicatorSize = 14.0f,
    };
}

void ContentBrowserPanel::setupSideBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .classes = {"content-browser-section"},
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, TOP_BAR_HEIGHT),
                    .size = Amethyst::UDim2(0.0f, SIDE_BAR_WIDTH, 1.0f, -TOP_BAR_HEIGHT),
                },
        },
        [this](Amethyst::FrameScope &side) {
            m_sideBarPane = &side.component;
            auto *ll = m_sideBarPane->addExtension<Amethyst::UIListLayout>();
            ll->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;

            side.collapsibleHeader(
                {
                    .classes = {"component-header"},
                    .base =
                        {
                            .clipsDescendants = true,
                            .layoutOrder = 0,
                        },
                    .header = s_sidebarHeaderStyle(),
                    .title = "Project",
                },
                [this](Amethyst::CollapsibleHeaderScope &ch) {
                    m_projectSection = &ch.component;
                    ch.component.onToggled = [this](bool) { layoutSideBar(); };
                    ch.treeView(
                        {
                            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                        },
                        [this](Amethyst::TreeViewScope &tv) {
                            m_directoryTree = &tv.component;
                            tv.column("", 1.0f);
                        });
                });
            side.collapsibleHeader(
                {
                    .classes = {"component-header"},
                    .base =
                        {
                            .clipsDescendants = true,
                            .layoutOrder = 1,
                        },
                    .header = s_sidebarHeaderStyle(),
                    .title = "Recent",
                },
                [this](Amethyst::CollapsibleHeaderScope &ch) {
                    m_recentSection = &ch.component;
                    ch.component.onToggled = [this](bool) { layoutSideBar(); };
                });
        });

    layoutSideBar();
}

void ContentBrowserPanel::layoutSideBar()
{
    // Recent holds nothing yet, so it is its header at either state and Project takes whatever is left
    const float recentHeight = SECTION_HEADER_HEIGHT;
    m_recentSection->setBaseProperties({.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, recentHeight)});

    const bool projectExpanded = static_cast<bool>(m_projectSection->getCollapsibleHeaderProperties().expanded);
    m_projectSection->setBaseProperties({
        .size = projectExpanded ? Amethyst::UDim2(1.0f, 0.0f, 1.0f, -recentHeight)
                                : Amethyst::UDim2(1.0f, 0.0f, 0.0f, SECTION_HEADER_HEIGHT),
    });
}

void ContentBrowserPanel::setupContentArea()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, SIDE_BAR_WIDTH, 0.0f, TOP_BAR_HEIGHT),
                    .size = Amethyst::UDim2(1.0f, -SIDE_BAR_WIDTH, 1.0f, -TOP_BAR_HEIGHT),
                },
        },
        [this](Amethyst::FrameScope &content) {
            m_contentPane = &content.component;
            auto *ll = m_contentPane->addExtension<Amethyst::UIListLayout>();
            ll->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;

            // Search / options bar (section 5)
            content.frame(
                {
                    .classes = {"content-browser-section"},
                    .base =
                        {
                            .layoutOrder = 0,
                            .padding = {},
                            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, SEARCH_BAR_HEIGHT),
                        },
                },
                [this](Amethyst::FrameScope &options) {
                    m_searchBar = &options.component;

                    options.frame(
                        {
                            .classes = {"searchbar"},
                            .base =
                                {
                                    .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.5f, 0.0f),
                                    .size = Amethyst::UDim2(0.8f, -2.0f * CONTENT_PADDING, 1.0f, -8.0f),
                                },
                        },
                        [this](Amethyst::FrameScope &field) {
                            field.imageLabel({
                                .base =
                                    {
                                        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                        .position = Amethyst::UDim2(0.0f, 8.0f, 0.5f, 0.0f),
                                        .size = Amethyst::UDim2::fromOffset(14.0f, 14.0f),
                                    },
                                .style = {.backgroundTransparency = 1.0f},
                                .image = {.imageColor = Amethyst::Color4(0.5f, 0.5f, 0.5f, 1.0f)},
                                .svg = Icons::SVG_SEARCH,
                            });
                            field.textInput(
                                {
                                    .base =
                                        {
                                            .position = Amethyst::UDim2(0.0f, 28.0f, 0.0f, 0.0f),
                                            .size = Amethyst::UDim2(1.0f, -34.0f, 1.0f, 0.0f),
                                        },
                                    .style = {.backgroundTransparency = 1.0f},
                                    .textInput = {.text = {.textYAlignment = Amethyst::TextYAlignment::CENTER}},
                                    .placeholder = "Search...",
                                },
                                [this](Amethyst::TextInputScope &ti) {
                                    m_searchInput = &ti.component;
                                    ti.component.onTextChanged = [this](const std::string &text) { onSearchTextChanged(text); };
                                });
                        });
                });

            content.scrollingFrame(
                {
                    .classes = {"grid-sink"},
                    .base =
                        {
                            .clipsDescendants = true,
                            .layoutOrder = 1,
                            .padding = Amethyst::UDim4::fromOffset(CONTENT_PADDING),
                            .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(SEARCH_BAR_HEIGHT + STATUS_BAR_HEIGHT)),
                        },
                    .scroll =
                        {
                            .scrollAxis = Amethyst::ScrollAxis::Y,
                            .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                            .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                        },
                },
                [this](Amethyst::ScrollingFrameScope &sf) {
                    m_contentContainer = &sf.component;
                    m_contentContainer->track(m_contentContainer->onInputBeganCb.connect([this](const Amethyst::InputObject &io) {
                        if (io.type != Amethyst::InputType::MOUSE_BUTTON_2) {
                            return;
                        }
                        showAddMenu(Amethyst::vec2(io.position.x, io.position.y));
                    }));
                    auto *gridLayout = sf.component.addExtension<Amethyst::UIGridLayout>();
                    gridLayout->cellSize = Amethyst::UDim2::fromOffset(TILE_MIN_WIDTH, TILE_MIN_WIDTH / TILE_ASPECT);
                    gridLayout->cellPadding = Amethyst::UDim2::fromOffset(CONTENT_PADDING, CONTENT_PADDING);
                    gridLayout->flexCells = true;
                    gridLayout->maxCellWidth = TILE_MAX_WIDTH;
                    gridLayout->cellAspectRatio = TILE_ASPECT;
                    gridLayout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    gridLayout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                    gridLayout->startCorner = Amethyst::StartCorner::TOP_LEFT;
                });

            // Status bar (item count)
            content.frame(
                {
                    .classes = {"status-bar"},
                    .base =
                        {
                            .layoutOrder = 2,
                            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, STATUS_BAR_HEIGHT),
                        },
                },
                [this](Amethyst::FrameScope &status) {
                    status.textLabel(
                        {
                            .classes = {"status-bar"},
                            .base =
                                {
                                    .position = Amethyst::UDim2(0.0f, 10.0f, 0.0f, 0.0f),
                                    .size = Amethyst::UDim2(1.0f, -10.0f, 1.0f, 0.0f),
                                },
                            .style = {.backgroundTransparency = 1.0f},
                            .label = "0 items",
                        },
                        [this](Amethyst::TextLabelScope &lbl) { m_statusLabel = &lbl.component; });
                });
        });
}

void ContentBrowserPanel::setupContextMenu()
{
    m_contextMenu = m_root->add<Amethyst::ContextMenu>();

    m_addMenu = m_root->add<Amethyst::ContextMenu>();
    m_addMenu->addClass("add-asset-menu");
    m_addMenu->placement = Amethyst::PopupPlacement::ABOVE;
    m_addMenu->popupWidth = ADD_MENU_WIDTH;
    // the rows differ in height, which is what the item count caps the menu against
    m_addMenu->maxVisibleItems = INT_MAX;
    m_addMenu->setRowFactories({
        .action = [] { return std::make_unique<AddAssetContextMenuAIV>(); },
        .separator = [] { return std::make_unique<ViewportContextMenuSIV>(); },
    });
}

void ContentBrowserPanel::showAddMenu(Amethyst::vec2 pos)
{
    if (m_addMenu == nullptr) {
        return;
    }
    m_addMenu->setItems(buildAddMenuItems());
    m_addMenu->showAt(pos);
}

MenuItems ContentBrowserPanel::buildAddMenuItems()
{
    MenuItems items;

    items.push_back(ViewportContextMenuSID::create("Folder"));
    items.push_back(AddAssetContextMenuAID::createIconRow("New Folder", Icons::SVG_FOLDER_PLUS,
                                                          [this]() { beginCreate(CONTENT_EDIT_FOLDER); }));
    items.push_back(AddAssetContextMenuAID::createIconRow("Refresh", Icons::SVG_REFRESH, [this]() { refresh(); }));

    items.push_back(ViewportContextMenuSID::create("Create Basic Asset"));
    items.push_back(
        AddAssetContextMenuAID::createAssetRow("Material", Rapture::ASSET_MATERIAL, []() { s_createAssetStub("material"); }));
    items.push_back(AddAssetContextMenuAID::createAssetRow("World", Rapture::ASSET_WORLD, []() { s_createAssetStub("world"); }));

    items.push_back(ViewportContextMenuSID::create("Create Advanced Asset"));
    for (auto &advanced : buildAdvancedAddItems()) {
        items.push_back(std::move(advanced));
    }

    return items;
}

MenuItems ContentBrowserPanel::buildAdvancedAddItems()
{
    // a row is labelled with the class it creates, so a class is named once and never relabelled
    auto appendClass = [this](MenuItems &into, const Rapture::TypeInfo &type) {
        into.push_back(AddAssetContextMenuAID::createPlainRow(
            std::string(type.name), [this, &type]() { beginCreate(CONTENT_EDIT_MODULE, &type); }));
    };

    MenuItems controllers;
    appendClass(controllers, Rapture::CameraController::staticType());
    appendClass(controllers, Rapture::PlayerController::staticType());

    MenuItems lights;
    appendClass(lights, Rapture::DirectionalLight3D::staticType());
    appendClass(lights, Rapture::PointLight3D::staticType());
    appendClass(lights, Rapture::SpotLight3D::staticType());

    MenuItems scenes;
    scenes.push_back(AddAssetContextMenuAID::createPlainRow("Level", []() { s_createAssetStub("level"); }));
    appendClass(scenes, Rapture::StaticMesh3D::staticType());
    appendClass(scenes, Rapture::Camera3D::staticType());
    appendClass(scenes, Rapture::SpringArm3D::staticType());
    scenes.push_back(Amethyst::makeSubmenuItem("Lights", std::move(lights)));
    scenes.push_back(Amethyst::makeSubmenuItem("Controllers", std::move(controllers)));

    MenuItems shaders;
    shaders.push_back(AddAssetContextMenuAID::createPlainRow("Compute Shader", []() { s_createAssetStub("compute shader"); }));
    shaders.push_back(AddAssetContextMenuAID::createPlainRow("Graphics Shader", []() { s_createAssetStub("graphics shader"); }));

    MenuItems textures;
    textures.push_back(AddAssetContextMenuAID::createPlainRow("Texture", []() { s_createAssetStub("texture"); }));
    textures.push_back(AddAssetContextMenuAID::createPlainRow("Cubemap", []() { s_createAssetStub("cubemap"); }));

    MenuItems items;
    items.push_back(Amethyst::makeSubmenuItem("Scenes", std::move(scenes)));
    items.push_back(Amethyst::makeSubmenuItem("Shaders", std::move(shaders)));
    items.push_back(Amethyst::makeSubmenuItem("Textures", std::move(textures)));

    return items;
}

void ContentBrowserPanel::beginCreate(ContentEditKind kind, const Rapture::TypeInfo *objectClass)
{
    if (kind == CONTENT_EDIT_MODULE && objectClass == nullptr) {
        RP_ERROR("a module needs a class to create");
        return;
    }

    m_edit = {};
    m_edit.kind = kind;
    m_edit.objectClass = objectClass;
    m_edit.initialName = kind == CONTENT_EDIT_FOLDER ? s_uniqueFolderName(m_currentDirectory)
                                                     : s_uniqueAssetName(m_currentDirectory, objectClass->name, Rapture::ASSET_MODULE);
    refresh();
}

void ContentBrowserPanel::beginRename(size_t index, const std::filesystem::path &target)
{
    if (index >= m_contentItemPool.size()) {
        return;
    }

    m_edit = {};
    m_edit.kind = CONTENT_EDIT_RENAME;
    m_edit.itemIndex = index;
    m_edit.target = target;
    startEditing(m_contentItemPool[index], target.stem().string());
}

void ContentBrowserPanel::startEditing(ContentItemComponents &item, const std::string &initialName)
{
    if (item.nameInput == nullptr) {
        item.nameInput = item.footer->add<Amethyst::TextInput>();
        item.nameInput->addClass("content-browser-card-name-input");
        item.nameInput->setBaseProperties({
            .position = Amethyst::UDim2(0.0f, 4.0f, 0.0f, 6.0f),
            .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 22.0f),
            .zIndex = 3,
        });
        item.nameInput->onEnterPressed = [this]() { commitEdit(); };
        item.nameInput->onFocusLost = [this]() { commitEdit(); };
    }

    item.name->setBaseProperties({.visible = false});
    item.nameInput->setBaseProperties({.visible = true});
    item.nameInput->setText(initialName);
    item.nameInput->focus();
    item.nameInput->selectAll();
}

void ContentBrowserPanel::commitEdit()
{
    if (m_edit.kind == CONTENT_EDIT_NONE) {
        return;
    }

    // taken before the work below, so the refresh that follows cannot come back round through onFocusLost
    ContentEdit edit = m_edit;
    m_edit = {};

    std::string name;
    if (edit.itemIndex < m_contentItemPool.size() && m_contentItemPool[edit.itemIndex].nameInput != nullptr) {
        name = m_contentItemPool[edit.itemIndex].nameInput->getText();
    }

    if (!name.empty()) {
        switch (edit.kind) {
        case CONTENT_EDIT_FOLDER:
            createFolder(name);
            break;
        case CONTENT_EDIT_MODULE:
            createModule(*edit.objectClass, name);
            break;
        case CONTENT_EDIT_RENAME:
            renameItem(edit.target, name);
            break;
        default:
            break;
        }
    }

    refresh();
}

void ContentBrowserPanel::createModule(const Rapture::TypeInfo &type, std::string_view name)
{
    // objects only exist inside a scene, so the asset is written from one built to be thrown away
    Rapture::Scene authoring{std::string(name)};
    Rapture::SceneObject *root = Rapture::InstanceRegistry::createObject(type.name, authoring, name).release();
    if (root == nullptr) {
        RP_ERROR("'{}' is not a registered scene object class", type.name);
        return;
    }
    authoring.root()->addChild(std::unique_ptr<Rapture::SceneObject>(root));

    auto document = std::make_unique<Rapture::SerialDocument>();
    root->serialize(document->root());
    document->freeze();

    Rapture::AssetImportDataRequest request;
    request.data = Rapture::ModuleImportData{std::move(document)};
    request.output = m_currentDirectory;
    request.name = std::string(name);

    if (!Rapture::AssetManager::importAsset(std::move(request))) {
        RP_ERROR("Could not create a '{}' named '{}' in '{}'", type.name, name, m_currentDirectory.string());
    }
}

void ContentBrowserPanel::createFolder(std::string_view name)
{
    std::error_code ec;
    std::filesystem::create_directory(m_currentDirectory / name, ec);
    if (ec) {
        RP_ERROR("Could not create '{}': {}", name, ec.message());
    }
}

void ContentBrowserPanel::renameItem(const std::filesystem::path &target, std::string_view name)
{
    std::filesystem::path renamed = target.parent_path() / (std::string(name) + target.extension().string());
    if (renamed == target) {
        return;
    }

    if (std::filesystem::exists(renamed)) {
        RP_ERROR("'{}' already exists", renamed.filename().string());
        return;
    }

    std::error_code ec;
    std::filesystem::rename(target, renamed, ec);
    if (ec) {
        RP_ERROR("Could not rename '{}': {}", target.filename().string(), ec.message());
    }
}

void ContentBrowserPanel::showContextMenu(Amethyst::vec2 pos, std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items)
{
    if (m_contextMenu == nullptr) {
        return;
    }
    m_contextMenu->setItems(std::move(items));
    m_contextMenu->showAt(pos);
}

// the asset types that have a workspace to open in
static bool s_opensInWorkspace(Rapture::AssetType type)
{
    return type == Rapture::ASSET_MODULE || type == Rapture::ASSET_MATERIAL_INSTANCE ||
           type == Rapture::ASSET_STATIC_MESH || type == Rapture::ASSET_SKELETAL_MESH || type == Rapture::ASSET_SKELETON;
}

std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> ContentBrowserPanel::assetActions(Rapture::AssetType type,
                                                                                                Rapture::AssetHandle handle)
{
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;

    if (s_opensInWorkspace(type)) {
        items.push_back(Amethyst::makeActionItem("Open", [this, handle]() {
            if (m_services.openAssetWorkspace) {
                m_services.openAssetWorkspace(handle);
            }
        }));
    }

    switch (type) {
    case Rapture::ASSET_MODULE:
        items.push_back(
            Amethyst::makeActionItem("Load in scene", [this, handle]() { s_spawnModule(handle, m_scene); }));
        break;
    default:
        break;
    }
    return items;
}

void ContentBrowserPanel::rebuildBreadcrumb()
{
    if (m_breadcrumbBar == nullptr) {
        return;
    }
    m_breadcrumbBar->removeAllChildren();

    std::vector<std::pair<std::string, std::filesystem::path>> segments;
    segments.emplace_back("All", m_baseDirectory);

    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(m_currentDirectory, m_baseDirectory, ec);
    if (!ec && !rel.empty() && rel.native() != ".." && rel.string().rfind("..", 0) != 0) {
        std::filesystem::path accumulated = m_baseDirectory;
        for (const auto &part : rel) {
            if (part == ".") {
                continue;
            }
            accumulated /= part;
            segments.emplace_back(part.string(), accumulated);
        }
    }

    uint32_t order = 0;
    Amethyst::UIScope scope(*m_breadcrumbBar);
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) {
            scope.imageLabel({
                .base =
                    {
                        .layoutOrder = order++,
                        .size = Amethyst::UDim2::fromOffset(12.0f, 12.0f),
                    },
                .style = {.backgroundTransparency = 1.0f},
                .svg = Icons::SVG_CARET_RIGHT,
            });
        }

        const std::string &label = segments[i].first;
        const std::filesystem::path &target = segments[i].second;
        bool isLast = (i == segments.size() - 1);

        scope.textButton(
            {
                .base =
                    {
                        .layoutOrder = order++,
                        .size = Amethyst::UDim2(0.0f, s_estimateTextWidth(label, 12.0f), 0.0f, 22.0f),
                    },
                .style = {.backgroundTransparency = 1.0f, .cornerRadius = 3.0f},
                .text =
                    {
                        .fontSize = 12.0f,
                        .textXAlignment = Amethyst::TextXAlignment::CENTER,
                        .textYAlignment = Amethyst::TextYAlignment::CENTER,
                    },
                .label = label,
            },
            [this, target](Amethyst::TextButtonScope &b) {
                b.component.onMouseButton1ClickCb = [this, target]() {
                    navigateToDirectory(target);
                    return Amethyst::EventResult::CONSUMED;
                };
            });
    }
}

void ContentBrowserPanel::refresh()
{
    m_selectedItem = SIZE_MAX;
    refreshFileBrowser();
    rebuildBreadcrumb();
}

void ContentBrowserPanel::setBaseDirectory(const std::filesystem::path &path)
{
    m_baseDirectory = path;
    m_currentDirectory = path;
    m_navigationHistory.clear();
    m_navigationHistory.push_back(m_currentDirectory);
    m_historyIndex = 0;
    buildDirectoryTree();
    refresh();
}

void ContentBrowserPanel::refreshFileBrowser()
{
    if (!std::filesystem::exists(m_currentDirectory)) {
        releasePoolItems(0);
        updateStatus(0);
        return;
    }

    std::vector<std::filesystem::directory_entry> entries(std::filesystem::directory_iterator(m_currentDirectory),
                                                          std::filesystem::directory_iterator{});

    std::sort(entries.begin(), entries.end(),
              [](const std::filesystem::directory_entry &a, const std::filesystem::directory_entry &b) {
                  bool aDir = a.is_directory();
                  bool bDir = b.is_directory();
                  if (aDir != bDir) return aDir > bDir;
                  return a.path().filename().string() < b.path().filename().string();
              });

    size_t index = 0;

    for (const auto &entry : entries) {
        bool isDir = entry.is_directory();
        std::string filename = entry.path().filename().string();

        const Rapture::AssetMetadata *metadata = nullptr;
        Rapture::AssetHandle assetHandle = Rapture::INVALID_ASSET_HANDLE;
        if (!isDir && Rapture::Asset_isRaptureExtension(entry.path().extension().string())) {
            assetHandle = Rapture::AssetManager::findAssetByPath(entry.path());
            if (assetHandle == Rapture::INVALID_ASSET_HANDLE) {
                continue;
            }
            metadata = &Rapture::AssetManager::getAssetMetadata(assetHandle);
        }

        std::string displayName = metadata != nullptr ? metadata->getName() : filename;
        if (!m_searchFilter.empty() && s_normalizeForSearch(displayName).find(m_searchFilter) == std::string::npos) {
            continue;
        }

        auto &item = acquirePoolItem(index);
        item.container->setBaseProperties({.layoutOrder = static_cast<uint32_t>(index)});

        if (isDir) {
            item.icon->setSvg(Icons::SVG_FOLDER);
            item.icon->setImageStyleProperties({.imageColor = Amethyst::Color4(0.85f, 0.72f, 0.4f, 1.0f)});
            layoutCardFooter(item, false);
            item.type->setBaseProperties({.visible = false});
        } else if (metadata != nullptr) {
            item.icon->setSvg(Asset_iconForType(metadata->assetType, metadata->authoredClass));
            item.icon->setImageStyleProperties({.imageColor = COL_ICON});
            layoutCardFooter(item, true);
            item.typeBar->setBaseStyleProperties({.backgroundColor = Asset_colorForType(metadata->assetType)});
            item.type->setBaseProperties({.visible = true});
            item.type->setText(s_typeLabel(metadata->assetType, metadata->authoredClass));
        } else {
            item.icon->setSvg(Icons::SVG_SCRIPT);
            item.icon->setImageStyleProperties({.imageColor = COL_ICON});
            layoutCardFooter(item, false);
            item.type->setBaseProperties({.visible = true});
            std::string extension = entry.path().extension().string();
            item.type->setText(extension.empty() ? "file" : extension);
        }

        item.name->setText(displayName);

        TooltipRows tooltipRows;
        tooltipRows.emplace_back("Name", displayName);
        if (isDir) {
            tooltipRows.emplace_back("Type", "Folder");
        } else {
            tooltipRows.emplace_back("Type", metadata != nullptr ? s_typeLabel(metadata->assetType, metadata->authoredClass)
                                                                 : entry.path().extension().string());
            std::error_code sizeEc;
            uintmax_t bytes = std::filesystem::file_size(entry.path(), sizeEc);
            if (!sizeEc) {
                tooltipRows.emplace_back("Size", Rapture::StringFormat_bytesToUnitString(bytes));
            }
        }
        item.tooltip->build = [rows = std::move(tooltipRows)](Amethyst::Tooltip &surface) { s_fillTooltip(surface, rows); };

        size_t itemIndex = index;
        item.action->onMouseButton1ClickCb = [this, itemIndex]() {
            selectItem(itemIndex);
            return Amethyst::EventResult::CONSUMED;
        };

        if (isDir) {
            std::filesystem::path dirPath = entry.path();
            item.action->onMouseButton1DoubleClickCb = [this, dirPath](int32_t x, int32_t y) {
                (void)x;
                (void)y;
                navigateToDirectory(dirPath);
                return Amethyst::EventResult::CONSUMED;
            };
        } else if (assetHandle != Rapture::INVALID_ASSET_HANDLE) {
            item.action->onMouseButton1DoubleClickCb = [this, assetHandle](int32_t x, int32_t y) {
                (void)x;
                (void)y;
                if (m_services.openAssetWorkspace) {
                    m_services.openAssetWorkspace(assetHandle);
                }
                return Amethyst::EventResult::CONSUMED;
            };
        } else {
            item.action->onMouseButton1DoubleClickCb = nullptr;
        }

        Rapture::AssetType assetType = metadata != nullptr ? metadata->assetType : Rapture::ASSET_NONE;
        bool isAsset = metadata != nullptr;
        std::filesystem::path itemPath = entry.path();
        item.action->onMouseButton2DownCb = [this, isDir, isAsset, assetType, assetHandle, itemIndex, itemPath](int32_t x,
                                                                                                                int32_t y) {
            std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
            if (isAsset) {
                items = assetActions(assetType, assetHandle);
            } else if (!isDir) {
                items.push_back(Amethyst::makeActionItem("Import", [] {}));
            }
            if (!items.empty()) {
                items.push_back(Amethyst::makeSeparatorItem());
            }
            items.push_back(
                Amethyst::makeActionItem("Rename", [this, itemIndex, itemPath]() { beginRename(itemIndex, itemPath); }));
            items.push_back(Amethyst::makeActionItem("Delete", [] {}));
            showContextMenu(Amethyst::vec2(static_cast<float>(x), static_cast<float>(y)), std::move(items));
            return Amethyst::EventResult::CONSUMED;
        };

        index++;
    }

    if (m_edit.kind == CONTENT_EDIT_FOLDER || m_edit.kind == CONTENT_EDIT_MODULE) {
        auto &item = acquirePoolItem(index);
        item.container->setBaseProperties({.layoutOrder = static_cast<uint32_t>(index)});
        item.action->onMouseButton1ClickCb = nullptr;
        item.action->onMouseButton2DownCb = nullptr;

        if (m_edit.kind == CONTENT_EDIT_FOLDER) {
            item.icon->setSvg(Icons::SVG_FOLDER);
            layoutCardFooter(item, false);
            item.type->setBaseProperties({.visible = false});
        } else {
            item.icon->setSvg(Asset_iconForType(Rapture::ASSET_MODULE, m_edit.objectClass));
            item.icon->setImageStyleProperties({.imageColor = COL_ICON});
            layoutCardFooter(item, true);
            item.typeBar->setBaseStyleProperties({.backgroundColor = Asset_colorForType(Rapture::ASSET_MODULE)});
            item.type->setBaseProperties({.visible = true});
            item.type->setText(s_typeLabel(Rapture::ASSET_MODULE, m_edit.objectClass));
        }

        m_edit.itemIndex = index;
        startEditing(item, m_edit.initialName);
        index++;
    }

    releasePoolItems(index);
    updateStatus(index);
}

void ContentBrowserPanel::layoutCardFooter(ContentItemComponents &item, bool hasTypeBar)
{
    const float barHeight = hasTypeBar ? TILE_TYPEBAR_HEIGHT : 0.0f;

    item.typeBar->setBaseProperties({.visible = hasTypeBar});
    item.footer->setBaseProperties({
        .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -barHeight),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TILE_FOOTER_HEIGHT + TILE_TYPEBAR_HEIGHT - barHeight),
    });
}

void ContentBrowserPanel::updateStatus(size_t itemCount)
{
    if (m_statusLabel == nullptr) {
        return;
    }
    size_t selected = (m_selectedItem == SIZE_MAX) ? 0 : 1;
    std::string text = std::to_string(itemCount) + " items (" + std::to_string(selected) + " selected)";
    m_statusLabel->setText(text);
}

static std::unique_ptr<Amethyst::TextButton> s_makeTreeCell(const std::string &label)
{
    auto btn = std::make_unique<Amethyst::TextButton>();
    btn->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
    btn->setBaseStyleProperties({.backgroundColor = COL_HOVER, .backgroundTransparency = 1.0f});
    btn->setTextStyleProperties({.fontSize = 13.0f});
    btn->setText(label);
    auto *raw = btn.get();
    raw->track(raw->onHoverChanged.connect(
        [raw](bool hovered) { raw->setBaseStyleProperties({.backgroundTransparency = hovered ? 0.0f : 1.0f}); }));
    return btn;
}

void ContentBrowserPanel::buildDirectoryTree()
{
    m_directoryTree->clear();

    if (std::filesystem::exists(m_baseDirectory)) {
        m_directoryTree->addRow(0);
        {
            std::filesystem::path base = m_baseDirectory;
            auto btn = s_makeTreeCell(m_baseDirectory.filename().string());
            btn->onMouseButton1ClickCb = [this, base]() {
                navigateToDirectory(base);
                return Amethyst::EventResult::CONSUMED;
            };
            m_directoryTree->nextCell(std::move(btn));
        }
        buildFilesSubtree(m_baseDirectory, 1);
    }
}

void ContentBrowserPanel::buildFilesSubtree(const std::filesystem::path &path, uint16_t depth)
{
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_directory()) continue;

        m_directoryTree->addRow(depth);

        std::filesystem::path dirPath = entry.path();
        auto btn = s_makeTreeCell(entry.path().filename().string());
        btn->onMouseButton1ClickCb = [this, dirPath]() {
            navigateToDirectory(dirPath);
            return Amethyst::EventResult::CONSUMED;
        };
        m_directoryTree->nextCell(std::move(btn));

        buildFilesSubtree(entry.path(), static_cast<uint16_t>(depth + 1));
    }
}

void ContentBrowserPanel::navigateToDirectory(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) return;

    if (m_historyIndex < m_navigationHistory.size() - 1) {
        m_navigationHistory.erase(m_navigationHistory.begin() + m_historyIndex + 1, m_navigationHistory.end());
    }

    m_currentDirectory = path;
    m_navigationHistory.push_back(m_currentDirectory);
    m_historyIndex = m_navigationHistory.size() - 1;

    refresh();
}

void ContentBrowserPanel::navigateBack()
{
    if (m_historyIndex > 0) {
        m_historyIndex--;
        m_currentDirectory = m_navigationHistory[m_historyIndex];
        refresh();
    }
}

void ContentBrowserPanel::navigateForward()
{
    if (m_historyIndex < m_navigationHistory.size() - 1) {
        m_historyIndex++;
        m_currentDirectory = m_navigationHistory[m_historyIndex];
        refresh();
    }
}

void ContentBrowserPanel::onSearchTextChanged(const std::string &text)
{
    m_searchFilter = s_normalizeForSearch(text);
    refresh();
}

void ContentBrowserPanel::selectItem(size_t index)
{
    if (m_selectedItem == index) {
        return;
    }

    /*
     * TODO: check if this is safe, and maybe use something else? instead of active? since it would be the same as selected but,
     * reads very differently
     * auto &newCard = m_contentItemPool[index];
     * newCard.container->setGuiState(newCard.container->getGuiState() | Amethyst::GUI_STATE_ACTIVE);
     *
     * auto &oldCard = m_contentItemPool[m_selectedItem];
     * oldCard.container->setGuiState(oldCard.container->getGuiState() & ~Amethyst::GUI_STATE_ACTIVE);
     */

    if (m_selectedItem < m_contentItemPool.size()) {
        applyItemSelection(m_contentItemPool[m_selectedItem], false);
    }

    m_selectedItem = index;

    if (m_selectedItem < m_contentItemPool.size()) {
        applyItemSelection(m_contentItemPool[m_selectedItem], true);
    }

    updateStatus(m_contentItemPool.size());
}

void ContentBrowserPanel::applyItemSelection(ContentItemComponents &item, bool selected)
{
    s_setActive(*item.container, selected);
    s_setActive(*item.footer, selected);
}

ContentBrowserPanel::ContentItemComponents &ContentBrowserPanel::acquirePoolItem(size_t index)
{
    if (index >= m_contentItemPool.size()) {
        // The InvisibleButton must be the topmost hittable node in the tile, so the visual
        // children are marked non-interactable; otherwise they swallow the click/hover.
        size_t slot = m_contentItemPool.size();
        ContentItemComponents item;

        item.container = m_contentContainer->add<Amethyst::Frame>();
        item.container->setClasses({"content-browser-card-well"});

        item.action = item.container->add<Amethyst::InvisibleButton>();
        item.action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
        item.tooltip = item.action->addExtension<Amethyst::UITooltip>();

        item.thumbWell = item.action->add<Amethyst::Frame>();
        item.thumbWell->setBaseProperties({
            .interactable = false,
            .position = Amethyst::UDim2::fromScale(0.0f),
            .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(TILE_FOOTER_HEIGHT + TILE_TYPEBAR_HEIGHT)),
            .zIndex = 1,
        });
        // opaque so a selected card's accent stops at the well rather than running behind the icon
        item.thumbWell->addClass("content-browser-card-thumb");

        auto *thumbAspect = item.thumbWell->addExtension<Amethyst::UIAspectRatioConstraint>();
        thumbAspect->aspectRatio = 1.0f;
        thumbAspect->dominantAxis = Amethyst::DominantAxis::WIDTH;

        item.icon = item.thumbWell->add<Amethyst::ImageLabel>();
        item.icon->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
            .interactable = false,
            .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
            .size = Amethyst::UDim2::fromOffset(TILE_ICON_SIZE, TILE_ICON_SIZE),
            .zIndex = 2,
        });
        item.icon->setBaseStyleProperties({.backgroundTransparency = 1.0f});
        item.icon->setImageStyleProperties({.imageColor = COL_ICON});

        item.footer = item.container->add<Amethyst::Frame>();
        item.footer->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
            .interactable = false,
            .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -TILE_TYPEBAR_HEIGHT),
            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TILE_FOOTER_HEIGHT),
            .zIndex = 1,
        });
        item.footer->addClass("content-browser-card-footer");

        item.name = item.footer->add<Amethyst::TextLabel>();
        item.name->addClass("content-browser-card-footer");
        item.name->setBaseProperties({
            .interactable = false,
            .position = Amethyst::UDim2(0.0f, 4.0f, 0.0f, 6.0f),
            .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 22.0f),
            .zIndex = 2,
        });
        item.name->setBaseStyleProperties({.backgroundTransparency = 1.0f});

        item.type = item.footer->add<Amethyst::TextLabel>();
        item.type->addClass("content-browser-card-footer-type");
        item.type->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
            .interactable = false,
            .position = Amethyst::UDim2(0.0f, 4.0f, 1.0f, -4.0f),
            .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 14.0f),
            .zIndex = 2,
        });

        item.typeBar = item.action->add<Amethyst::Frame>();
        item.typeBar->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
            .interactable = false,
            .position = Amethyst::UDim2::fromScale(0.0f, 1.0f),
            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TILE_TYPEBAR_HEIGHT),
            .zIndex = 2,
        });

        item.attached = true;
        m_contentItemPool.push_back(item);
    }

    auto &item = m_contentItemPool[index];
    if (!item.attached) {
        item.container->setBaseProperties({.visible = true});
        item.attached = true;
    }

    // a pooled tile can come back from having been named or selected, so it starts every refresh showing neither
    if (item.nameInput != nullptr) {
        item.nameInput->setBaseProperties({.visible = false});
    }
    item.name->setBaseProperties({.visible = true});
    applyItemSelection(item, index == m_selectedItem);
    return item;
}

void ContentBrowserPanel::releasePoolItems(size_t fromIndex)
{
    for (size_t i = fromIndex; i < m_contentItemPool.size(); i++) {
        auto &item = m_contentItemPool[i];
        if (item.attached) {
            item.container->setBaseProperties({.visible = false});
            item.attached = false;
        }
    }
}
