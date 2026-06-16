#include "ContentBrowserPanel.h"
#include "asset_manager/AssetManager.h"
#include "logging/Log.h"

#include <algorithm>
#include <components/extensions/ui_grid_layout.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

#define TOP_PANE_HEIGHT_SCALE   0.08f
#define SIDE_BAR_WIDTH_SCALE    0.2f
#define MODE_BTN_HEIGHT_SCALE   0.05f
#define SEARCH_BAR_HEIGHT_SCALE 0.06f

static void s_wirePressDarken(Amethyst::TextButton *btn)
{
    btn->onMouseButton1DownCb = [btn](uint32_t, uint32_t) {
        btn->setBaseStyleProperties({.backgroundColor = btn->getBaseStyleProperties().backgroundColor * 0.7f});
        return Amethyst::EventResult::CONSUMED;
    };
    btn->onMouseButton1UpCb = [btn](uint32_t, uint32_t) {
        btn->setBaseStyleProperties({.backgroundColor = btn->getBaseStyleProperties().backgroundColor / 0.7f});
        return Amethyst::EventResult::CONSUMED;
    };
}

ContentBrowserPanel::ContentBrowserPanel(Amethyst::TabBar *tabBar) : m_hostTabBar(tabBar)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_root->name = "Content Browser";

    m_baseDirectory = std::filesystem::current_path();
    m_currentDirectory = m_baseDirectory;
    m_navigationHistory.push_back(m_currentDirectory);

    setupTopBar();
    setupSideBar();
    setupContentArea();

    m_hostTabBar->addTab(std::move(root), "Content Browser");
}

ContentBrowserPanel::~ContentBrowserPanel()
{
    if (m_hostTabBar != nullptr && m_root != nullptr) {
        m_hostTabBar->removeTab(m_root);
    }
}

void ContentBrowserPanel::setupTopBar()
{
    const Amethyst::TextStyleProperties btnTextStyle{
        .textXAlignment = Amethyst::TextXAlignment::CENTER,
        .textYAlignment = Amethyst::TextYAlignment::CENTER,
    };

    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f),
                    .size = Amethyst::UDim2::fromScale(1.0f, TOP_PANE_HEIGHT_SCALE),
                },
        },
        [this, &btnTextStyle](Amethyst::FrameScope &top) {
            m_topBarPane = &top.component;
            auto *layout = top.component.addExtension<Amethyst::UIListLayout>();
            layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
            layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
            layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
            layout->innerPadding = Amethyst::UDim::fromScale(0.005f);

            top.textButton(
                {
                    .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromScale(0.06f, 0.7f)},
                    .text = btnTextStyle,
                    .label = "<",
                },
                [this](Amethyst::TextButtonScope &b) {
                    m_goBackBtn = &b.component;
                    b.component.onMouseButton1ClickCb = [this]() {
                        navigateBack();
                        return Amethyst::EventResult::CONSUMED;
                    };
                    s_wirePressDarken(m_goBackBtn);
                });
            top.textButton(
                {
                    .base = {.layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.06f, 0.7f)},
                    .text = btnTextStyle,
                    .label = ">",
                },
                [this](Amethyst::TextButtonScope &b) {
                    m_goForwardBtn = &b.component;
                    b.component.onMouseButton1ClickCb = [this]() {
                        navigateForward();
                        return Amethyst::EventResult::CONSUMED;
                    };
                    s_wirePressDarken(m_goForwardBtn);
                });
            top.textButton(
                {
                    .base = {.layoutOrder = 2, .size = Amethyst::UDim2::fromScale(0.08f, 0.7f)},
                    .text = btnTextStyle,
                    .label = "+ Add",
                },
                [this](Amethyst::TextButtonScope &b) {
                    m_addBtn = &b.component;
                    s_wirePressDarken(m_addBtn);
                });
            top.textButton(
                {
                    .base = {.layoutOrder = 3, .size = Amethyst::UDim2::fromScale(0.08f, 0.7f)},
                    .text = btnTextStyle,
                    .label = "Import",
                },
                [this](Amethyst::TextButtonScope &b) {
                    m_importBtn = &b.component;
                    s_wirePressDarken(m_importBtn);
                });
        });
}

void ContentBrowserPanel::setupSideBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f, TOP_PANE_HEIGHT_SCALE),
                    .size = Amethyst::UDim2::fromScale(SIDE_BAR_WIDTH_SCALE, 1.0f - TOP_PANE_HEIGHT_SCALE),
                },
        },
        [this](Amethyst::FrameScope &side) {
            m_sideBarPane = &side.component;
            side.textButton(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2::fromScale(0.0f, 0.0f),
                            .size = Amethyst::UDim2::fromScale(1.0f, MODE_BTN_HEIGHT_SCALE),
                        },
                    .text =
                        {
                            .textXAlignment = Amethyst::TextXAlignment::CENTER,
                            .textYAlignment = Amethyst::TextYAlignment::CENTER,
                        },
                    .label = "Mode: Assets",
                },
                [this](Amethyst::TextButtonScope &b) {
                    m_modeToggleBtn = &b.component;
                    b.component.onMouseButton1ClickCb = [this]() {
                        toggleBrowseMode();
                        return Amethyst::EventResult::CONSUMED;
                    };
                    s_wirePressDarken(m_modeToggleBtn);
                });
            side.scrollingFrame(
                {
                    .base =
                        {
                            .clipsDescendants = true,
                            .position = Amethyst::UDim2::fromScale(0.0f, MODE_BTN_HEIGHT_SCALE),
                            .size = Amethyst::UDim2::fromScale(1.0f, 1.0f - MODE_BTN_HEIGHT_SCALE),
                        },
                    .scroll =
                        {
                            .scrollAxis = Amethyst::ScrollAxis::Y,
                            .canvasSize = Amethyst::UDim2::fromScale(1.0f, 2.0f),
                        },
                },
                [this](Amethyst::ScrollingFrameScope &sf) {
                    m_directoryTreeContainer = &sf.component;
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
}

void ContentBrowserPanel::setupContentArea()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(SIDE_BAR_WIDTH_SCALE, TOP_PANE_HEIGHT_SCALE),
                    .size = Amethyst::UDim2::fromScale(1.0f - SIDE_BAR_WIDTH_SCALE, 1.0f - TOP_PANE_HEIGHT_SCALE),
                },
        },
        [this](Amethyst::FrameScope &content) {
            m_contentPane = &content.component;
            content.frame(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2::fromScale(0.0f),
                            .size = Amethyst::UDim2::fromScale(1.0f, SEARCH_BAR_HEIGHT_SCALE),
                        },
                },
                [this](Amethyst::FrameScope &options) {
                    m_contentOptionsBar = &options.component;
                    options.textInput(
                        {
                            .base =
                                {
                                    .position = Amethyst::UDim2::fromScale(0.01f, 0.1f),
                                    .size = Amethyst::UDim2::fromScale(0.5f, 0.8f),
                                },
                            .placeholder = "Search...",
                        },
                        [this](Amethyst::TextInputScope &ti) {
                            m_searchInput = &ti.component;
                            ti.component.onTextChanged = [this](const std::string &text) { onSearchTextChanged(text); };
                        });
                });
            content.scrollingFrame(
                {
                    .base =
                        {
                            .clipsDescendants = true,
                            .position = Amethyst::UDim2::fromScale(0.0f, SEARCH_BAR_HEIGHT_SCALE),
                            .size = Amethyst::UDim2::fromScale(1.0f, 1.0f - SEARCH_BAR_HEIGHT_SCALE),
                        },
                    .scroll =
                        {
                            .scrollAxis = Amethyst::ScrollAxis::Y,
                            .canvasSize = Amethyst::UDim2::fromScale(1.0f, 2.0f),
                        },
                },
                [this](Amethyst::ScrollingFrameScope &sf) {
                    m_contentContainer = &sf.component;
                    auto *gridLayout = sf.component.addExtension<Amethyst::UIGridLayout>();
                    gridLayout->cellSize = Amethyst::UDim2::fromScale(0.12f, 0.15f);
                    gridLayout->cellPadding = Amethyst::UDim2::fromScale(0.01f, 0.01f);
                    gridLayout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                });
        });
}

void ContentBrowserPanel::refresh()
{
    if (m_browseMode == BrowseMode::ASSETS) {
        refreshAssetBrowser();
    } else {
        refreshFileBrowser();
    }
    refreshDirectoryTree();
}

void ContentBrowserPanel::setBaseDirectory(const std::filesystem::path &path)
{
    m_baseDirectory = path;
    m_currentDirectory = path;
    m_navigationHistory.clear();
    m_navigationHistory.push_back(m_currentDirectory);
    m_historyIndex = 0;
    refresh();
}

void ContentBrowserPanel::refreshAssetBrowser()
{
    const auto &registry = Rapture::AssetManager::getAssetRegistry();
    size_t index = 0;

    for (const auto &[handle, metadata] : registry) {
        if (!metadata) continue;

        if (!m_searchFilter.empty()) {
            std::string name = metadata->getName();
            if (name.find(m_searchFilter) == std::string::npos) continue;
        }

        auto &item = acquirePoolItem(index);

        item.container->setBaseProperties({.layoutOrder = static_cast<uint32_t>(index)});
        item.container->setBaseStyleProperties({.backgroundColor = Amethyst::Color3(0.2f)});

        item.thumbnail->setBaseProperties({
            .position = Amethyst::UDim2::fromScale(0.05f, 0.05f),
            .size = Amethyst::UDim2::fromScale(0.9f, 0.7f),
        });
        item.thumbnail->setBaseStyleProperties({.backgroundColor = Amethyst::Color3(0.3f)});

        item.typeIndicator->setBaseProperties({
            .position = Amethyst::UDim2::fromScale(0.05f, 0.76f),
            .size = Amethyst::UDim2::fromScale(0.9f, 0.04f),
            .visible = true,
        });

        Amethyst::Color3 typeColor;
        switch (metadata->assetType) {
        case Rapture::AssetType::TEXTURE:
            typeColor = Amethyst::Color3(0.9f, 0.55f, 0.8f);
            break;
        case Rapture::AssetType::CUBEMAP:
            typeColor = Amethyst::Color3(0.4f, 0.75f, 1.0f);
            break;
        case Rapture::AssetType::SHADER:
            typeColor = Amethyst::Color3(0.5f, 0.9f, 0.5f);
            break;
        case Rapture::AssetType::MATERIAL:
            typeColor = Amethyst::Color3(0.75f, 0.45f, 0.95f);
            break;
        case Rapture::AssetType::MESH:
            typeColor = Amethyst::Color3(0.9f, 0.65f, 0.35f);
            break;
        case Rapture::AssetType::MODEL:
            typeColor = Amethyst::Color3(0.9f, 0.55f, 0.2f);
            break;
        case Rapture::AssetType::ANIMATION:
            typeColor = Amethyst::Color3(0.95f, 0.8f, 0.25f);
            break;
        case Rapture::AssetType::AUDIO:
            typeColor = Amethyst::Color3(0.3f, 0.85f, 0.85f);
            break;
        case Rapture::AssetType::VIDEO:
            typeColor = Amethyst::Color3(0.95f, 0.35f, 0.35f);
            break;
        case Rapture::AssetType::SCENE:
            typeColor = Amethyst::Color3(0.85f, 0.7f, 0.15f);
            break;
        default:
            typeColor = Amethyst::Color3(0.5f, 0.5f, 0.5f);
            break;
        }
        item.typeIndicator->setBaseStyleProperties({.backgroundColor = typeColor});

        item.name->setBaseProperties({
            .position = Amethyst::UDim2::fromScale(0.0f, 0.82f),
            .size = Amethyst::UDim2::fromScale(1.0f, 0.18f),
        });
        item.name->setBaseStyleProperties({.backgroundTransparency = 1.0f});
        item.name->setTextStyleProperties({
            .fontSize = 11.0f,
            .textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f),
            .textXAlignment = Amethyst::TextXAlignment::CENTER,
        });
        item.name->setText(metadata->getName());

        std::string assetName = metadata->getName();
        item.action->onMouseButton1ClickCb = [assetName]() {
            Rapture::RP_INFO("clicked asset '{0}'", assetName);
            return Amethyst::EventResult::CONSUMED;
        };

        index++;
    }

    releasePoolItems(index);
}

void ContentBrowserPanel::refreshFileBrowser()
{
    if (!std::filesystem::exists(m_currentDirectory)) {
        releasePoolItems(0);
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
        std::string filename = entry.path().filename().string();

        if (!m_searchFilter.empty()) {
            if (filename.find(m_searchFilter) == std::string::npos) continue;
        }

        auto &item = acquirePoolItem(index);

        item.container->setBaseProperties({.layoutOrder = static_cast<uint32_t>(index)});
        item.container->setBaseStyleProperties({.backgroundColor = Amethyst::Color3(0.2f)});

        item.thumbnail->setBaseProperties({
            .position = Amethyst::UDim2::fromScale(0.05f, 0.05f),
            .size = Amethyst::UDim2::fromScale(0.9f, 0.7f),
        });
        item.thumbnail->setBaseStyleProperties({
            .backgroundColor = entry.is_directory() ? Amethyst::Color3(0.5f, 0.4f, 0.2f) : Amethyst::Color3(0.92f, 0.88f, 0.78f),
        });

        item.typeIndicator->setBaseProperties({.visible = false});

        item.name->setBaseProperties({
            .position = Amethyst::UDim2::fromScale(0.0f, 0.82f),
            .size = Amethyst::UDim2::fromScale(1.0f, 0.18f),
        });
        item.name->setBaseStyleProperties({.backgroundTransparency = 1.0f});
        item.name->setTextStyleProperties({
            .fontSize = 11.0f,
            .textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f),
            .textXAlignment = Amethyst::TextXAlignment::CENTER,
        });
        item.name->setText(filename);

        if (entry.is_directory()) {
            std::filesystem::path dirPath = entry.path();
            item.action->onMouseButton1ClickCb = [this, dirPath]() {
                navigateToDirectory(dirPath);
                return Amethyst::EventResult::CONSUMED;
            };
        } else {
            item.action->onMouseButton1ClickCb = [filename]() {
                Rapture::RP_INFO("clicked file '{0}'", filename);
                return Amethyst::EventResult::CONSUMED;
            };
        }

        index++;
    }

    releasePoolItems(index);
}

void ContentBrowserPanel::refreshDirectoryTree()
{
    m_directoryTree->clear();

    if (m_browseMode == BrowseMode::FILES && std::filesystem::exists(m_baseDirectory)) {
        buildDirectoryTree(m_baseDirectory, 0);
    }
}

void ContentBrowserPanel::buildDirectoryTree(const std::filesystem::path &path, uint16_t depth)
{
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_directory()) continue;

        m_directoryTree->addRow(depth);

        auto btn = std::make_unique<Amethyst::TextButton>();
        btn->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
        btn->setBaseStyleProperties({.backgroundTransparency = 1.0f});
        btn->setTextStyleProperties({.fontSize = 14.0f, .textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f)});
        btn->setText(entry.path().filename().string());

        std::filesystem::path dirPath = entry.path();
        btn->onMouseButton1ClickCb = [this, dirPath]() {
            navigateToDirectory(dirPath);
            return Amethyst::EventResult::CONSUMED;
        };
        m_directoryTree->nextCell(std::move(btn));

        buildDirectoryTree(entry.path(), static_cast<uint16_t>(depth + 1));
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
    m_searchFilter = text;
    refresh();
}

void ContentBrowserPanel::toggleBrowseMode()
{
    if (m_browseMode == BrowseMode::ASSETS) {
        m_browseMode = BrowseMode::FILES;
        m_modeToggleBtn->setText("Mode: Files");
    } else {
        m_browseMode = BrowseMode::ASSETS;
        m_modeToggleBtn->setText("Mode: Assets");
    }
    refresh();
}

ContentBrowserPanel::ContentItemComponents &ContentBrowserPanel::acquirePoolItem(size_t index)
{
    if (index >= m_contentItemPool.size()) {
        ContentItemComponents item;
        item.container = m_contentContainer->add<Amethyst::Frame>();
        item.action = item.container->add<Amethyst::InvisibleButton>();
        item.action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
        item.thumbnail = item.action->add<Amethyst::ImageLabel>();
        item.thumbnail->setBaseProperties({.zIndex = 2});
        item.typeIndicator = item.action->add<Amethyst::Frame>();
        item.typeIndicator->setBaseProperties({.zIndex = 2});
        item.name = item.action->add<Amethyst::TextLabel>();
        item.name->setBaseProperties({.zIndex = 2});
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
