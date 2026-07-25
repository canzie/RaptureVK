#include "ContentBrowserPanel.h"
#include "Icons.h"
#include "asset_manager/AssetManager.h"
#include "components/systems/Prefab.h"
#include "logging/Log.h"
#include "scenes/Project.h"
#include "scenes/entities/Entity.h"
#include "window_context/Application.h"

#include <algorithm>
#include <cctype>
#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/extensions/ui_aspect_ratio_constraint.h>
#include <components/extensions/ui_grid_layout.h>
#include <components/extensions/ui_list_layout.h>
#include <components/frame.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <memory>
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

#define COL_TOP_BAR   Amethyst::Color3::fromHex(0x252525)
#define COL_SIDE_BAR  Amethyst::Color3::fromHex(0x2b2b2b)
#define COL_GRID_BG   Amethyst::Color3::fromHex(0x1b1b1b)
#define COL_TILE      Amethyst::Color3::fromHex(0x303030)
#define COL_TILE_WELL Amethyst::Color3::fromHex(0x3c3c3c)
#define COL_TILE_FOOT Amethyst::Color3::fromHex(0x232323)
#define COL_SELECTION Amethyst::Color3(0.13f, 0.45f, 0.85f)
#define COL_SEPARATOR Amethyst::Color3::fromHex(0x181818)
#define COL_TEXT      Amethyst::Color4(0.85f, 0.85f, 0.85f, 1.0f)
#define COL_TEXT_DIM  Amethyst::Color4(0.6f, 0.6f, 0.6f, 1.0f)
#define COL_ICON      Amethyst::Color4(0.8f, 0.8f, 0.8f, 1.0f)
#define COL_BTN       Amethyst::Color3::fromHex(0x3a3a3a)
#define COL_BTN_HOVER Amethyst::Color3::fromHex(0x4d4d4d)
#define COL_HOVER     Amethyst::Color3::fromHex(0x4d4d4d)

static constexpr float CONTENT_PADDING = 10.0f;

static const char *s_iconForAssetType(Rapture::AssetType type)
{
    switch (type) {
    case Rapture::AssetType::TEXTURE:
        return Icons::SVG_LAYERS;
    case Rapture::AssetType::CUBEMAP:
        return Icons::SVG_CUBE;
    case Rapture::AssetType::SHADER:
        return Icons::SVG_SCRIPT;
    case Rapture::AssetType::MATERIAL:
    case Rapture::AssetType::MATERIAL_INSTANCE:
        return Icons::SVG_MATERIAL;
    case Rapture::AssetType::MESH:
        return Icons::SVG_MESH;
    case Rapture::AssetType::PREFAB:
        return Icons::SVG_CUBE;
    case Rapture::AssetType::ANIMATION:
        return Icons::SVG_PLAY;
    case Rapture::AssetType::AUDIO:
        return Icons::SVG_AUDIO;
    case Rapture::AssetType::VIDEO:
        return Icons::SVG_CAMERA;
    case Rapture::AssetType::SCENE:
        return Icons::SVG_SCENE;
    default:
        return Icons::SVG_COPY;
    }
}

static Amethyst::Color3 s_colorForAssetType(Rapture::AssetType type)
{
    switch (type) {
    case Rapture::AssetType::TEXTURE:
        return Amethyst::Color3(0.92f, 0.40f, 0.78f); // magenta
    case Rapture::AssetType::CUBEMAP:
        return Amethyst::Color3(0.30f, 0.68f, 0.98f); // azure
    case Rapture::AssetType::SHADER:
        return Amethyst::Color3(0.45f, 0.85f, 0.45f); // green
    case Rapture::AssetType::MATERIAL:
    case Rapture::AssetType::MATERIAL_INSTANCE:
        return Amethyst::Color3(0.68f, 0.45f, 0.95f); // violet
    case Rapture::AssetType::MESH:
        return Amethyst::Color3(0.95f, 0.60f, 0.25f); // orange
    case Rapture::AssetType::PREFAB:
        return Amethyst::Color3(0.50f, 0.50f, 0.95f); // periwinkle
    case Rapture::AssetType::ANIMATION:
        return Amethyst::Color3(0.95f, 0.82f, 0.30f); // yellow
    case Rapture::AssetType::AUDIO:
        return Amethyst::Color3(0.25f, 0.82f, 0.72f); // teal
    case Rapture::AssetType::VIDEO:
        return Amethyst::Color3(0.95f, 0.35f, 0.35f); // red
    case Rapture::AssetType::SCENE:
        return Amethyst::Color3(0.72f, 0.85f, 0.30f); // lime
    default:
        return Amethyst::Color3(0.55f, 0.55f, 0.55f); // gray
    }
}

static void s_loadPrefabIntoScene(Rapture::AssetHandle handle, Rapture::Scene *scene)
{
    if (scene == nullptr) {
        RP_WARN("No scene to load the prefab into");
        return;
    }
    if (!Rapture::Prefab::instantiate(Rapture::AssetManager::getAsset(handle), scene).isValid()) {
        RP_WARN("Failed to load prefab into the scene");
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

static void s_wireIconHover(Amethyst::ImageButton *btn)
{
    btn->track(btn->onHoverChanged.connect([btn](bool hovered) {
        btn->setImageStyleProperties({.imageColor = hovered ? Amethyst::Color4(0.95f, 0.95f, 0.95f, 1.0f) : COL_ICON});
        btn->setBaseStyleProperties({.backgroundTransparency = hovered ? 0.8f : 1.0f});
    }));
}

static void s_wireButtonHover(Amethyst::TextButton *btn)
{
    btn->setBaseStyleProperties({.backgroundColor = COL_BTN});
    btn->track(btn->onHoverChanged.connect(
        [btn](bool hovered) { btn->setBaseStyleProperties({.backgroundColor = hovered ? COL_BTN_HOVER : COL_BTN}); }));
}

static void s_wireGhostHover(Amethyst::TextButton *btn)
{
    btn->track(btn->onHoverChanged.connect([btn](bool hovered) {
        btn->setBaseStyleProperties({.backgroundColor = COL_BTN_HOVER, .backgroundTransparency = hovered ? 0.0f : 1.0f});
    }));
}

ContentBrowserPanel::ContentBrowserPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context)
    : Panel("Content Browser", context)
{
    m_isDocked = true;
    m_scene = context.scene;

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
    m_scene = context.scene;
}

void ContentBrowserPanel::buildContent()
{
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });

    m_root->setBaseStyleProperties({.backgroundColor = COL_GRID_BG});

    m_baseDirectory = Rapture::Application::getInstance().getProject().getProjectDirectory();
    m_currentDirectory = m_baseDirectory;
    m_navigationHistory.push_back(m_currentDirectory);

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
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TOP_BAR_HEIGHT),
                },
            .style = {.backgroundColor = COL_TOP_BAR},
        },
        [this](Amethyst::FrameScope &top) {
            m_topBarPane = &top.component;

            const Amethyst::TextStylePropertiesArgs btnTextStyle{
                .fontSize = 12.0f,
                .textColor = COL_TEXT,
                .textXAlignment = Amethyst::TextXAlignment::CENTER,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
            };

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
                [this, &btnTextStyle](Amethyst::FrameScope &cluster) {
                    auto *layout = cluster.component.addExtension<Amethyst::UIListLayout>();
                    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                    layout->innerPadding = Amethyst::UDim::fromOffset(4.0f);

                    cluster.textButton(
                        {
                            .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(56.0f, 24.0f)},
                            .style = {.cornerRadius = 3.0f},
                            .text = btnTextStyle,
                            .label = "+ Add",
                        },
                        [this](Amethyst::TextButtonScope &b) {
                            m_addBtn = &b.component;
                            s_wireButtonHover(m_addBtn);
                        });
                    cluster.textButton(
                        {
                            .base = {.layoutOrder = 1, .size = Amethyst::UDim2::fromOffset(58.0f, 24.0f)},
                            .style = {.cornerRadius = 3.0f},
                            .text = btnTextStyle,
                            .label = "Import",
                        },
                        [this](Amethyst::TextButtonScope &b) {
                            m_importBtn = &b.component;
                            s_wireButtonHover(m_importBtn);
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
                            s_wireIconHover(m_goBackBtn);
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
                            s_wireIconHover(m_goForwardBtn);
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
                [this](Amethyst::ImageButtonScope &b) {
                    m_settingsBtn = &b.component;
                    s_wireIconHover(m_settingsBtn);
                });

            if (!m_isDocked) {
                top.textButton(
                    {
                        .base =
                            {
                                .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                                .position = Amethyst::UDim2(1.0f, -38.0f, 0.5f, 0.0f),
                                .size = Amethyst::UDim2::fromOffset(100.0f, 24.0f),
                            },
                        .style = {.cornerRadius = 3.0f},
                        .text = {.fontSize = 12.0f,
                                 .textColor = COL_TEXT,
                                 .textXAlignment = Amethyst::TextXAlignment::CENTER,
                                 .textYAlignment = Amethyst::TextYAlignment::CENTER},
                        .label = "Dock in layout",
                    },
                    [this](Amethyst::TextButtonScope &b) {
                        s_wireButtonHover(&b.component);
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
        .titleStyle =
            {
                .fontSize = 12.0f,
                .textColor = COL_TEXT,
                .textXAlignment = Amethyst::TextXAlignment::LEFT,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
            },
        .headerHeight = SECTION_HEADER_HEIGHT,
        .indicatorSize = 14.0f,
        .indicatorColor = COL_TEXT_DIM,
    };
}

void ContentBrowserPanel::setupSideBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .classes = {"panel"},
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, TOP_BAR_HEIGHT),
                    .size = Amethyst::UDim2(0.0f, SIDE_BAR_WIDTH, 1.0f, -TOP_BAR_HEIGHT),
                },
        },
        [this](Amethyst::FrameScope &side) {
            m_sideBarPane = &side.component;

            side.collapsibleHeader(
                {
                    .classes = {"component-header"},
                    .base =
                        {
                            .clipsDescendants = true,
                            .position = Amethyst::UDim2::fromScale(0.0f),
                            .size = Amethyst::UDim2::fromScale(1.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f},
                    .header = s_sidebarHeaderStyle(),
                    .title = "Project",
                },
                [this](Amethyst::CollapsibleHeaderScope &ch) {
                    m_projectHeader = &ch.component;
                    ch.scrollingFrame(
                        {
                            .base =
                                {
                                    .clipsDescendants = true,
                                    .padding = Amethyst::UDim4{{},
                                                               {},
                                                               Amethyst::UDim::fromOffset(CONTENT_PADDING),
                                                               Amethyst::UDim::fromOffset(CONTENT_PADDING)},
                                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, SECTION_HEADER_HEIGHT),
                                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -SECTION_HEADER_HEIGHT),
                                },
                            .scroll =
                                {
                                    .scrollAxis = Amethyst::ScrollAxis::Y,
                                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                                    .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                                },
                        },
                        [this](Amethyst::ScrollingFrameScope &sf) {
                            m_directoryTreeContainer = &sf.component;
                            m_directoryTreeContainer->setBaseStyleProperties({.backgroundTransparency = 1.0f});
                            sf.treeView(
                                {
                                    .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                                },
                                [this](Amethyst::TreeViewScope &tv) {
                                    m_directoryTree = &tv.component;
                                    tv.column("", 1.0f);
                                });
                        });
                });
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
            .style = {.backgroundColor = COL_GRID_BG},
        },
        [this](Amethyst::FrameScope &content) {
            m_contentPane = &content.component;

            // Search / options bar (section 5)
            content.frame(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2::fromScale(0.0f),
                            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, SEARCH_BAR_HEIGHT),
                        },
                    .style = {.backgroundColor = COL_TOP_BAR},
                },
                [this](Amethyst::FrameScope &options) {
                    m_searchBar = &options.component;

                    options.frame(
                        {
                            .classes = {"searchbar"},
                            .base =
                                {
                                    .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
                                    .position = Amethyst::UDim2(0.5f, 0.0f, 0.5f, 0.0f),
                                    .size = Amethyst::UDim2(1.0f, -2.0f * CONTENT_PADDING, 1.0f, -8.0f),
                                },
                            .style = {.cornerRadius = 3.0f},
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
                    .base =
                        {
                            .clipsDescendants = true,
                            .padding = {Amethyst::UDim::fromOffset(CONTENT_PADDING), Amethyst::UDim::fromOffset(CONTENT_PADDING),
                                        Amethyst::UDim::fromOffset(CONTENT_PADDING), Amethyst::UDim::fromOffset(CONTENT_PADDING)},
                            .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, SEARCH_BAR_HEIGHT),
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
                    m_contentContainer->setBaseStyleProperties({.backgroundTransparency = 1.0f});
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
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
                            .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, 0.0f),
                            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, STATUS_BAR_HEIGHT),
                        },
                    .style = {.backgroundColor = COL_TOP_BAR},
                },
                [this](Amethyst::FrameScope &status) {
                    status.textLabel(
                        {
                            .base =
                                {
                                    .position = Amethyst::UDim2(0.0f, 10.0f, 0.0f, 0.0f),
                                    .size = Amethyst::UDim2(1.0f, -10.0f, 1.0f, 0.0f),
                                },
                            .style = {.backgroundTransparency = 1.0f},
                            .text =
                                {
                                    .fontSize = 11.0f,
                                    .textColor = COL_TEXT_DIM,
                                    .textXAlignment = Amethyst::TextXAlignment::LEFT,
                                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                                },
                            .label = "0 items",
                        },
                        [this](Amethyst::TextLabelScope &lbl) { m_statusLabel = &lbl.component; });
                });
        });
}

void ContentBrowserPanel::setupContextMenu()
{
    m_contextMenu = m_root->add<Amethyst::ContextMenu>();
}

void ContentBrowserPanel::showContextMenu(Amethyst::vec2 pos, std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items)
{
    if (m_contextMenu == nullptr) {
        return;
    }
    m_contextMenu->setItems(std::move(items));
    m_contextMenu->showAt(pos);
}

std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> ContentBrowserPanel::assetActions(Rapture::AssetType type,
                                                                                                Rapture::AssetHandle handle)
{
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    switch (type) {
    case Rapture::AssetType::PREFAB:
        items.push_back(Amethyst::makeActionItem("Load in scene", [this, handle]() { s_loadPrefabIntoScene(handle, m_scene); }));
        return items;
    default:
        return items;
    }
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
                .image = {.imageColor = COL_TEXT_DIM},
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
                        .textColor = isLast ? COL_TEXT : COL_TEXT_DIM,
                        .textXAlignment = Amethyst::TextXAlignment::CENTER,
                        .textYAlignment = Amethyst::TextYAlignment::CENTER,
                    },
                .label = label,
            },
            [this, target](Amethyst::TextButtonScope &b) {
                s_wireGhostHover(&b.component);
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
        if (!isDir && entry.path().extension() == ".rasset") {
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
            item.typeBar->setBaseProperties({.visible = false});
            item.type->setBaseProperties({.visible = false});
        } else if (metadata != nullptr) {
            item.icon->setSvg(s_iconForAssetType(metadata->assetType));
            item.icon->setImageStyleProperties({.imageColor = COL_ICON});
            item.typeBar->setBaseProperties({.visible = true});
            item.typeBar->setBaseStyleProperties({.backgroundColor = s_colorForAssetType(metadata->assetType)});
            item.type->setBaseProperties({.visible = true});
            item.type->setText(Rapture::AssetTypeToString(metadata->assetType));
        } else {
            item.icon->setSvg(Icons::SVG_SCRIPT);
            item.icon->setImageStyleProperties({.imageColor = COL_ICON});
            item.typeBar->setBaseProperties({.visible = false});
            item.type->setBaseProperties({.visible = true});
            std::string extension = entry.path().extension().string();
            item.type->setText(extension.empty() ? "file" : extension);
        }

        item.name->setText(displayName);

        size_t itemIndex = index;
        if (isDir) {
            std::filesystem::path dirPath = entry.path();
            item.action->onMouseButton1ClickCb = [this, dirPath]() {
                navigateToDirectory(dirPath);
                return Amethyst::EventResult::CONSUMED;
            };
        } else {
            item.action->onMouseButton1ClickCb = [this, itemIndex, displayName]() {
                selectItem(itemIndex);
                RP_INFO("selected '{0}'", displayName);
                return Amethyst::EventResult::CONSUMED;
            };
        }

        Rapture::AssetType assetType = metadata != nullptr ? metadata->assetType : Rapture::AssetType::NONE;
        bool isAsset = metadata != nullptr;
        item.action->onMouseButton2DownCb = [this, isDir, isAsset, assetType, assetHandle](int32_t x, int32_t y) {
            std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
            if (isAsset) {
                items = assetActions(assetType, assetHandle);
            } else if (!isDir) {
                items.push_back(Amethyst::makeActionItem("Import", [] {}));
            }
            if (!items.empty()) {
                items.push_back(Amethyst::makeSeparatorItem());
            }
            items.push_back(Amethyst::makeActionItem("Rename", [] {}));
            items.push_back(Amethyst::makeActionItem("Delete", [] {}));
            showContextMenu(Amethyst::vec2(static_cast<float>(x), static_cast<float>(y)), std::move(items));
            return Amethyst::EventResult::CONSUMED;
        };

        index++;
    }

    releasePoolItems(index);
    updateStatus(index);
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
    btn->setTextStyleProperties({.fontSize = 13.0f, .textColor = COL_TEXT});
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
    if (m_selectedItem != SIZE_MAX && m_selectedItem < m_contentItemPool.size()) {
        applyItemSelection(m_contentItemPool[m_selectedItem], false);
    }
    m_selectedItem = index;
    if (index < m_contentItemPool.size()) {
        applyItemSelection(m_contentItemPool[index], true);
    }
    updateStatus(m_contentItemPool.size());
}

void ContentBrowserPanel::applyItemSelection(ContentItemComponents &item, bool selected)
{
    item.container->setBaseStyleProperties({
        .borderPixelSize = selected ? 2.0f : 0.0f,
        .borderColor = COL_SELECTION,
    });
    item.footer->setBaseStyleProperties({.backgroundColor = selected ? COL_SELECTION : COL_TILE_FOOT});
}

void ContentBrowserPanel::applyItemHover(ContentItemComponents &item, bool hovered)
{
    item.container->setBaseStyleProperties({
        .borderPixelSize = hovered ? 1.0f : 0.0f,
        .borderColor = COL_HOVER,
    });
}

ContentBrowserPanel::ContentItemComponents &ContentBrowserPanel::acquirePoolItem(size_t index)
{
    if (index >= m_contentItemPool.size()) {
        // The InvisibleButton must be the topmost hittable node in the tile, so the visual
        // children are marked non-interactable; otherwise they swallow the click/hover.
        size_t slot = m_contentItemPool.size();
        ContentItemComponents item;

        item.container = m_contentContainer->add<Amethyst::Frame>();
        item.container->setClasses({"panel"});
        item.container->setBaseStyleProperties({
            .borderMode = Amethyst::BorderMode::OUTLINE,
            .borderPixelSize = 0.0f,
            .borderColor = COL_SELECTION,
            .cornerRadius = 3.0f,
        });

        item.action = item.container->add<Amethyst::InvisibleButton>();
        item.action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
        item.action->track(item.action->onHoverChanged.connect([this, slot](bool hovered) {
            if (slot >= m_contentItemPool.size() || m_selectedItem == slot) {
                return;
            }
            applyItemHover(m_contentItemPool[slot], hovered);
        }));

        item.thumbWell = item.action->add<Amethyst::Frame>();
        item.thumbWell->setBaseProperties({
            .interactable = false,
            .position = Amethyst::UDim2::fromScale(0.0f),
            .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(TILE_FOOTER_HEIGHT + TILE_TYPEBAR_HEIGHT)),
            .zIndex = 1,
        });
        item.thumbWell->setBaseStyleProperties({.backgroundColor = COL_TILE_WELL});
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

        item.footer = item.action->add<Amethyst::Frame>();
        item.footer->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
            .interactable = false,
            .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -TILE_TYPEBAR_HEIGHT),
            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TILE_FOOTER_HEIGHT),
            .zIndex = 1,
        });
        item.footer->setBaseStyleProperties({.backgroundColor = COL_TILE_FOOT});

        item.name = item.footer->add<Amethyst::TextLabel>();
        item.name->setBaseProperties({
            .interactable = false,
            .position = Amethyst::UDim2(0.0f, 4.0f, 0.0f, 6.0f),
            .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 22.0f),
            .zIndex = 2,
        });
        item.name->setBaseStyleProperties({.backgroundTransparency = 1.0f});
        item.name->setTextStyleProperties({
            .fontSize = 12.0f,
            .textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f),
            .textXAlignment = Amethyst::TextXAlignment::CENTER,
            .textYAlignment = Amethyst::TextYAlignment::CENTER,
            .textTruncate = Amethyst::TextTruncate::AT_END,
        });

        item.type = item.footer->add<Amethyst::TextLabel>();
        item.type->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
            .interactable = false,
            .position = Amethyst::UDim2(0.0f, 4.0f, 1.0f, -4.0f),
            .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 14.0f),
            .zIndex = 2,
        });
        item.type->setBaseStyleProperties({.backgroundTransparency = 1.0f});
        item.type->setTextStyleProperties({
            .fontSize = 10.0f,
            .textColor = Amethyst::Color4(0.6f, 0.6f, 0.6f, 1.0f),
            .textXAlignment = Amethyst::TextXAlignment::CENTER,
            .textYAlignment = Amethyst::TextYAlignment::CENTER,
            .textTruncate = Amethyst::TextTruncate::AT_END,
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
    item.container->setBaseStyleProperties({.borderPixelSize = 0.0f});
    item.footer->setBaseStyleProperties({.backgroundColor = COL_TILE_FOOT});
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
