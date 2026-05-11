#include "ContentBrowserPanel.h"
#include "asset_manager/AssetManager.h"
#include "logging/Log.h"
#include <algorithm>
#include <components/common.h>
#include <components/extensions/ui_grid_layout.h>
#include <components/extensions/ui_list_layout.h>

#define TOP_PANE_HEIGHT_SCALE   0.08f
#define SIDE_BAR_WIDTH_SCALE    0.2f
#define MODE_BTN_HEIGHT_SCALE   0.05f
#define SEARCH_BAR_HEIGHT_SCALE 0.06f

ContentBrowserPanel::ContentBrowserPanel(Amethyst::DockingLayer *dockingLayer) : m_dockingLayer(dockingLayer)
{
    auto root = std::make_unique<Amethyst::PanelLayer>();
    m_root = root.get();
    m_root->name = "Content Browser";
    m_root->markDirty();

    m_baseDirectory = std::filesystem::current_path();
    m_currentDirectory = m_baseDirectory;
    m_navigationHistory.push_back(m_currentDirectory);

    setupTopBar();
    setupSideBar();
    setupContentArea();

    m_dockingLayer->dock(std::move(root), glm::vec2(0.0f));
}

ContentBrowserPanel::~ContentBrowserPanel()
{
    if (m_dockingLayer && m_root) {
        m_dockingLayer->undock(m_root);
    }
}

void ContentBrowserPanel::setupTopBar()
{
    m_topBarPane = m_root->add<Amethyst::Frame>();
    m_topBarPane->position = Amethyst::UDim2::fromScale(0.0f);
    m_topBarPane->size = Amethyst::UDim2::fromScale(1.0f, TOP_PANE_HEIGHT_SCALE);
    auto *layout = m_topBarPane->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
    layout->innerPadding = Amethyst::UDim::fromScale(0.005f);
    m_topBarPane->markDirty();

    m_goBackBtn = m_topBarPane->add<Amethyst::TextButton>();
    m_goBackBtn->size = Amethyst::UDim2::fromScale(0.06f, 0.7f);
    m_goBackBtn->text = "<";
    m_goBackBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_goBackBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_goBackBtn->layoutOrder = 0;
    m_goBackBtn->onMouseButton1ClickCb = [this]() { navigateBack(); };
    m_goBackBtn->onMouseButton1DownCb = [this](uint32_t, uint32_t) {
        m_goBackBtn->backgroundColor = m_goBackBtn->backgroundColor * 0.7f;
        m_goBackBtn->markDirty();
    };
    m_goBackBtn->onMouseButton1UpCb = [this](uint32_t, uint32_t) {
        m_goBackBtn->backgroundColor = m_goBackBtn->backgroundColor / 0.7f;
        m_goBackBtn->markDirty();
    };
    m_goBackBtn->markDirty();

    m_goForwardBtn = m_topBarPane->add<Amethyst::TextButton>();
    m_goForwardBtn->size = Amethyst::UDim2::fromScale(0.06f, 0.7f);
    m_goForwardBtn->text = ">";
    m_goForwardBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_goForwardBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_goForwardBtn->layoutOrder = 1;
    m_goForwardBtn->onMouseButton1ClickCb = [this]() { navigateForward(); };
    m_goForwardBtn->onMouseButton1DownCb = [this](uint32_t, uint32_t) {
        m_goForwardBtn->backgroundColor = m_goForwardBtn->backgroundColor * 0.7f;
        m_goForwardBtn->markDirty();
    };
    m_goForwardBtn->onMouseButton1UpCb = [this](uint32_t, uint32_t) {
        m_goForwardBtn->backgroundColor = m_goForwardBtn->backgroundColor / 0.7f;
        m_goForwardBtn->markDirty();
    };
    m_goForwardBtn->markDirty();

    m_addBtn = m_topBarPane->add<Amethyst::TextButton>();
    m_addBtn->size = Amethyst::UDim2::fromScale(0.08f, 0.7f);
    m_addBtn->text = "+ Add";
    m_addBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_addBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_addBtn->layoutOrder = 2;
    m_addBtn->onMouseButton1DownCb = [this](uint32_t, uint32_t) {
        m_addBtn->backgroundColor = m_addBtn->backgroundColor * 0.7f;
        m_addBtn->markDirty();
    };
    m_addBtn->onMouseButton1UpCb = [this](uint32_t, uint32_t) {
        m_addBtn->backgroundColor = m_addBtn->backgroundColor / 0.7f;
        m_addBtn->markDirty();
    };
    m_addBtn->markDirty();

    m_importBtn = m_topBarPane->add<Amethyst::TextButton>();
    m_importBtn->size = Amethyst::UDim2::fromScale(0.08f, 0.7f);
    m_importBtn->text = "Import";
    m_importBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_importBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_importBtn->layoutOrder = 3;
    m_importBtn->onMouseButton1DownCb = [this](uint32_t, uint32_t) {
        m_importBtn->backgroundColor = m_importBtn->backgroundColor * 0.7f;
        m_importBtn->markDirty();
    };
    m_importBtn->onMouseButton1UpCb = [this](uint32_t, uint32_t) {
        m_importBtn->backgroundColor = m_importBtn->backgroundColor / 0.7f;
        m_importBtn->markDirty();
    };
    m_importBtn->markDirty();
}

void ContentBrowserPanel::setupSideBar()
{
    m_sideBarPane = m_root->add<Amethyst::Frame>();
    m_sideBarPane->position = Amethyst::UDim2::fromScale(0.0f, TOP_PANE_HEIGHT_SCALE);
    m_sideBarPane->size = Amethyst::UDim2::fromScale(SIDE_BAR_WIDTH_SCALE, 1.0f - TOP_PANE_HEIGHT_SCALE);
    m_sideBarPane->markDirty();

    m_modeToggleBtn = m_sideBarPane->add<Amethyst::TextButton>();
    m_modeToggleBtn->size = Amethyst::UDim2::fromScale(1.0f, MODE_BTN_HEIGHT_SCALE);
    m_modeToggleBtn->position = Amethyst::UDim2::fromScale(0.0f, 0.0f);
    m_modeToggleBtn->text = "Mode: Assets";
    m_modeToggleBtn->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_modeToggleBtn->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_modeToggleBtn->onMouseButton1ClickCb = [this]() { toggleBrowseMode(); };
    m_modeToggleBtn->onMouseButton1DownCb = [this](uint32_t, uint32_t) {
        m_modeToggleBtn->backgroundColor = m_modeToggleBtn->backgroundColor * 0.7f;
        m_modeToggleBtn->markDirty();
    };
    m_modeToggleBtn->onMouseButton1UpCb = [this](uint32_t, uint32_t) {
        m_modeToggleBtn->backgroundColor = m_modeToggleBtn->backgroundColor / 0.7f;
        m_modeToggleBtn->markDirty();
    };
    m_modeToggleBtn->markDirty();

    m_directoryTreeContainer = m_sideBarPane->add<Amethyst::ScrollingFrame>();
    m_directoryTreeContainer->position = Amethyst::UDim2::fromScale(0.0f, MODE_BTN_HEIGHT_SCALE);
    m_directoryTreeContainer->size = Amethyst::UDim2::fromScale(1.0f, 1.0f - MODE_BTN_HEIGHT_SCALE);
    m_directoryTreeContainer->clipsDescendants = true;
    m_directoryTreeContainer->scrollAxis = Amethyst::ScrollAxis::Y;
    m_directoryTreeContainer->canvasSize = Amethyst::UDim2::fromScale(1.0f, 2.0f);
    m_directoryTreeContainer->markDirty();

    m_directoryTree = m_directoryTreeContainer->add<Amethyst::TreeView>();
    m_directoryTree->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
    m_directoryTree->numCols = 1;
    m_directoryTree->onRowClicked = [this](uint32_t row) {
        // TODO: navigate to clicked directory
    };
    m_directoryTree->markDirty();
}

void ContentBrowserPanel::setupContentArea()
{
    m_contentPane = m_root->add<Amethyst::Frame>();
    m_contentPane->position = Amethyst::UDim2::fromScale(SIDE_BAR_WIDTH_SCALE, TOP_PANE_HEIGHT_SCALE);
    m_contentPane->size = Amethyst::UDim2::fromScale(1.0f - SIDE_BAR_WIDTH_SCALE, 1.0f - TOP_PANE_HEIGHT_SCALE);
    m_contentPane->markDirty();

    m_contentOptionsBar = m_contentPane->add<Amethyst::Frame>();
    m_contentOptionsBar->position = Amethyst::UDim2::fromScale(0.0f);
    m_contentOptionsBar->size = Amethyst::UDim2::fromScale(1.0f, SEARCH_BAR_HEIGHT_SCALE);
    m_contentOptionsBar->markDirty();

    m_searchInput = m_contentOptionsBar->add<Amethyst::TextInput>();
    m_searchInput->size = Amethyst::UDim2::fromScale(0.5f, 0.8f);
    m_searchInput->position = Amethyst::UDim2::fromScale(0.01f, 0.1f);
    m_searchInput->placeholderText = "Search...";
    m_searchInput->onTextChanged = [this](const std::string &text) { onSearchTextChanged(text); };
    m_searchInput->markDirty();

    m_contentContainer = m_contentPane->add<Amethyst::ScrollingFrame>();
    m_contentContainer->position = Amethyst::UDim2::fromScale(0.0f, SEARCH_BAR_HEIGHT_SCALE);
    m_contentContainer->size = Amethyst::UDim2::fromScale(1.0f, 1.0f - SEARCH_BAR_HEIGHT_SCALE);
    m_contentContainer->clipsDescendants = true;
    m_contentContainer->scrollAxis = Amethyst::ScrollAxis::Y;
    m_contentContainer->canvasSize = Amethyst::UDim2::fromScale(1.0f, 2.0f);
    auto *gridLayout = m_contentContainer->addExtension<Amethyst::UIGridLayout>();
    gridLayout->cellSize = Amethyst::UDim2::fromScale(0.12f, 0.15f);
    gridLayout->cellPadding = Amethyst::UDim2::fromScale(0.01f, 0.01f);
    gridLayout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    m_contentContainer->markDirty();
}

void ContentBrowserPanel::onUpdate(float deltaTime)
{
    m_searchInput->update(deltaTime);
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

        item.container->backgroundColor = Amethyst::Color3(0.2f);
        item.container->layoutOrder = static_cast<uint32_t>(index);
        item.container->markDirty();

        item.thumbnail->size = Amethyst::UDim2::fromScale(0.9f, 0.7f);
        item.thumbnail->position = Amethyst::UDim2::fromScale(0.05f, 0.05f);
        item.thumbnail->backgroundColor = Amethyst::Color3(0.3f);
        item.thumbnail->markDirty();

        item.typeIndicator->visible = true;
        item.typeIndicator->size = Amethyst::UDim2::fromScale(0.9f, 0.04f);
        item.typeIndicator->position = Amethyst::UDim2::fromScale(0.05f, 0.76f);

        switch (metadata->assetType) {
        case Rapture::AssetType::TEXTURE:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.9f, 0.55f, 0.8f);
            break;
        case Rapture::AssetType::CUBEMAP:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.4f, 0.75f, 1.0f);
            break;
        case Rapture::AssetType::SHADER:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.5f, 0.9f, 0.5f);
            break;
        case Rapture::AssetType::MATERIAL:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.75f, 0.45f, 0.95f);
            break;
        case Rapture::AssetType::MESH:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.9f, 0.65f, 0.35f);
            break;
        case Rapture::AssetType::MODEL:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.9f, 0.55f, 0.2f);
            break;
        case Rapture::AssetType::ANIMATION:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.95f, 0.8f, 0.25f);
            break;
        case Rapture::AssetType::AUDIO:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.3f, 0.85f, 0.85f);
            break;
        case Rapture::AssetType::VIDEO:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.95f, 0.35f, 0.35f);
            break;
        case Rapture::AssetType::SCENE:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.85f, 0.7f, 0.15f);
            break;
        default:
            item.typeIndicator->backgroundColor = Amethyst::Color3(0.5f, 0.5f, 0.5f);
            break;
        }

        item.typeIndicator->markDirty();

        item.name->size = Amethyst::UDim2::fromScale(1.0f, 0.18f);
        item.name->position = Amethyst::UDim2::fromScale(0.0f, 0.82f);
        item.name->text = metadata->getName();
        item.name->textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f);
        item.name->fontSize = 11.0f;
        item.name->textXAlignment = Amethyst::TextXAlignment::CENTER;
        item.name->backgroundTransparency = 1.0f;
        item.name->markDirty();

        std::string assetName = metadata->getName();
        item.action->onMouseButton1ClickCb = [assetName]() { Rapture::RP_INFO("clicked asset '{0}'", assetName); };
        item.action->markDirty();

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

        item.container->backgroundColor = Amethyst::Color3(0.2f);
        item.container->layoutOrder = static_cast<uint32_t>(index);
        item.container->markDirty();

        item.thumbnail->size = Amethyst::UDim2::fromScale(0.9f, 0.7f);
        item.thumbnail->position = Amethyst::UDim2::fromScale(0.05f, 0.05f);
        item.thumbnail->backgroundColor =
            entry.is_directory() ? Amethyst::Color3(0.5f, 0.4f, 0.2f) : Amethyst::Color3(0.92f, 0.88f, 0.78f);
        item.thumbnail->markDirty();

        item.typeIndicator->visible = false;
        item.typeIndicator->markDirty();

        item.name->size = Amethyst::UDim2::fromScale(1.0f, 0.18f);
        item.name->position = Amethyst::UDim2::fromScale(0.0f, 0.82f);
        item.name->text = filename;
        item.name->textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f);
        item.name->fontSize = 11.0f;
        item.name->textXAlignment = Amethyst::TextXAlignment::CENTER;
        item.name->backgroundTransparency = 1.0f;
        item.name->markDirty();

        if (entry.is_directory()) {
            std::filesystem::path dirPath = entry.path();
            item.action->onMouseButton1ClickCb = [this, dirPath]() { navigateToDirectory(dirPath); };
        } else {
            item.action->onMouseButton1ClickCb = [filename]() { Rapture::RP_INFO("clicked file '{0}'", filename); };
        }
        item.action->markDirty();

        index++;
    }

    releasePoolItems(index);
}

void ContentBrowserPanel::refreshDirectoryTree()
{
    m_directoryTree->clear();

    if (m_browseMode == BrowseMode::FILES && std::filesystem::exists(m_baseDirectory)) {
        buildDirectoryTree(m_baseDirectory, Amethyst::INVALID_ROW);
    }

    m_directoryTree->markDirty();
}

void ContentBrowserPanel::buildDirectoryTree(const std::filesystem::path &path, uint32_t parentRow)
{
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_directory()) continue;

        uint32_t rowIndex = m_directoryTree->beginRow(parentRow);

        auto *btn = m_directoryTree->add<Amethyst::TextButton>();
        btn->text = entry.path().filename().string();
        btn->textColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f);
        btn->backgroundTransparency = 1.0f;
        btn->fontSize = 14.0f;
        btn->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);

        std::filesystem::path dirPath = entry.path();
        btn->onMouseButton1ClickCb = [this, dirPath]() { navigateToDirectory(dirPath); };
        btn->markDirty();

        m_directoryTree->endRow();

        buildDirectoryTree(entry.path(), rowIndex);
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
        m_modeToggleBtn->text = "Mode: Files";
    } else {
        m_browseMode = BrowseMode::ASSETS;
        m_modeToggleBtn->text = "Mode: Assets";
    }
    m_modeToggleBtn->markDirty();
    refresh();
}

ContentBrowserPanel::ContentItemComponents &ContentBrowserPanel::acquirePoolItem(size_t index)
{
    if (index >= m_contentItemPool.size()) {
        ContentItemComponents item;
        item.container = m_contentContainer->add<Amethyst::Frame>();
        item.action = item.container->add<Amethyst::InvisibleButton>();
        item.action->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
        item.thumbnail = item.action->add<Amethyst::ImageLabel>();
        item.thumbnail->zIndex = 2;
        item.typeIndicator = item.action->add<Amethyst::Frame>();
        item.typeIndicator->zIndex = 2;
        item.name = item.action->add<Amethyst::TextLabel>();
        item.name->zIndex = 2;
        item.attached = true;
        m_contentItemPool.push_back(item);
    }

    auto &item = m_contentItemPool[index];
    if (!item.attached) {
        item.container->visible = true;
        item.container->markDirty();
        item.attached = true;
    }
    return item;
}

void ContentBrowserPanel::releasePoolItems(size_t fromIndex)
{
    for (size_t i = fromIndex; i < m_contentItemPool.size(); i++) {
        auto &item = m_contentItemPool[i];
        if (item.attached) {
            item.container->visible = false;
            item.container->markDirty();
            item.attached = false;
        }
    }
}
