#include "ContentBrowserPanel.h"
#include "Icons.h"
#include "asset_manager/AssetManager.h"
#include "components/systems/Prefab.h"
#include "layers/panels/components/asset_visuals.h"
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

static void s_loadPrefabIntoScene(Rapture::AssetHandle handle, Rapture::Scene *scene)
{
    if (scene == nullptr) {
        RP_WARN("No scene to load the prefab into");
        return;
    }
    if (Rapture::Prefab::instantiate(Rapture::AssetManager::getAsset(handle), scene) == nullptr) {
        RP_WARN("Failed to load prefab into the scene");
    }
}

static void s_openScene(Rapture::AssetHandle handle)
{
    auto &sceneManager = Rapture::Application::getInstance().getProject().getSceneManager();

    Rapture::Scene *scene = sceneManager.openScene(handle);
    if (scene == nullptr) {
        RP_WARN("Failed to open the scene");
        return;
    }

    sceneManager.activateScene(scene);
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
                        [this](Amethyst::TextButtonScope &b) { m_addBtn = &b.component; });
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
                            .size = Amethyst::UDim2::fromScale(1.0f, 0.8f),
                        },
                    .header = s_sidebarHeaderStyle(),
                    .title = "Project",
                },
                [this](Amethyst::CollapsibleHeaderScope &ch) {
                    ch.treeView(
                        {
                            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                        },
                        [this](Amethyst::TreeViewScope &tv) {
                            m_directoryTree = &tv.component;
                            tv.column("", 1.0f);
                        });
                });
            side.collapsibleHeader({
                .classes = {"component-header"},
                .base =
                    {
                        .clipsDescendants = true,
                        .size = Amethyst::UDim2::fromScale(1.0f, 0.2f),
                    },
                .header = s_sidebarHeaderStyle(),
                .title = "Recent",
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
            item.icon->setSvg(Asset_iconForType(metadata->assetType));
            item.icon->setImageStyleProperties({.imageColor = COL_ICON});
            item.typeBar->setBaseProperties({.visible = true});
            item.typeBar->setBaseStyleProperties({.backgroundColor = Asset_colorForType(metadata->assetType)});
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

    m_selectedItem = index;
    updateStatus(m_contentItemPool.size());
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

        item.thumbWell = item.action->add<Amethyst::Frame>();
        item.thumbWell->setBaseProperties({
            .interactable = false,
            .position = Amethyst::UDim2::fromScale(0.0f),
            .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(TILE_FOOTER_HEIGHT + TILE_TYPEBAR_HEIGHT)),
            .zIndex = 1,
        });
        item.thumbWell->setBaseStyleProperties({.backgroundTransparency = 1.0f});

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
