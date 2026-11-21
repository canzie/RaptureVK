#include "ContentBrowserPanel.h"
#include "Icons.h"
#include "context/WindowManager.h"
#include "context/WorkspaceManager.h"
#include "ui/AssetVisuals.h"
#include "ui/ControlTooltip.h"
#include "ui/controls/IconButton.h"
#include "ui/controls/IconLabelButton.h"
#include "ui/controls/SearchField.h"
#include "ui/menus/ContextMenuItems.h"
#include "workspaces/ScriptingWorkspace.h"
#include <rapture/app/Application.h>
#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/assets/asset_manager/AssetRegistry.h>
#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/StringFormat.h>
#include <rapture/scene/Project.h>
#include <rapture/scene/World.h>
#include <rapture/scene/instances/Camera3D.h>
#include <rapture/scene/instances/DirectionalLight3D.h>
#include <rapture/scene/instances/InstanceRegistry.h>
#include <rapture/scene/instances/Module.h>
#include <rapture/scene/instances/PointLight3D.h>
#include <rapture/scene/instances/SceneObject.h>
#include <rapture/scene/instances/SpotLight3D.h>
#include <rapture/scene/instances/SpringArm3D.h>
#include <rapture/scene/instances/StaticMesh3D.h>
#include <rapture/scene/instances/controllers/CameraController.h>
#include <rapture/scene/instances/controllers/PlayerController.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <components/common.h>
#include <components/context_menu_item.h>
#include <components/extensions/ui_flex_item.h>
#include <components/extensions/ui_grid_layout.h>
#include <components/extensions/ui_list_layout.h>
#include <components/extensions/ui_size_constraint.h>
#include <components/frame.h>
#include <components/input_interface.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <format>
#include <limits>
#include <memory>
#include <modules/color.h>
#include <modules/text_processor.h>
#include <optional>
#include <string_view>

enum ContentBrowserTooltip {
    CONTENT_BROWSER_TOOLTIP_ADD,
    CONTENT_BROWSER_TOOLTIP_IMPORT,
    CONTENT_BROWSER_TOOLTIP_BACK,
    CONTENT_BROWSER_TOOLTIP_FORWARD,
    CONTENT_BROWSER_TOOLTIP_UP,
    CONTENT_BROWSER_TOOLTIP_DOCK,
    CONTENT_BROWSER_TOOLTIP_SETTINGS,
    CONTENT_BROWSER_TOOLTIP_COUNT
};

static constexpr std::array<ControlTooltip, CONTENT_BROWSER_TOOLTIP_COUNT> CONTENT_BROWSER_TOOLTIPS = {{
    {.name = "Add", .description = "Creates a folder or an asset in this folder"},
    {.name = "Import", .description = "Brings a file from disk into this folder"},
    {.name = "Back", .description = "Back"},
    {.name = "Forward", .description = "Forward"},
    {.name = "Up", .description = "Up to the parent folder"},
    {.name = "Dock", .description = "Docks the browser into the workspace"},
    {.name = "Settings", .description = "Browser settings"},
}};

static constexpr float TOOLBAR_HEIGHT = 40.0f;
static constexpr float SEPARATOR_WIDTH = 2.0f;
static constexpr float BODY_TOP = TOOLBAR_HEIGHT + SEPARATOR_WIDTH;
static constexpr float SIDE_BAR_WIDTH = 218.0f;
static constexpr float CONTENT_LEFT = SIDE_BAR_WIDTH + SEPARATOR_WIDTH;
static constexpr float STATUS_BAR_HEIGHT = 24.0f;

static constexpr float ISLAND_PADDING = 12.0f;
static constexpr float TOOLBAR_CONTROL_HEIGHT = 28.0f;
static constexpr float NAV_BUTTON_WIDTH = 30.0f;
static constexpr float NAV_BUTTON_GAP = 2.0f;
static constexpr float NAV_ICON_SIZE = 17.0f;
static constexpr float SETTINGS_ICON_SIZE = 19.0f;
static constexpr float SEARCH_FIELD_MAX_WIDTH = 260.0f;
static constexpr float SEARCH_FIELD_MIN_WIDTH = 140.0f;
static constexpr float SEARCH_FIELD_HEIGHT = 26.0f;

static constexpr float CRUMB_HEIGHT = 24.0f;
static constexpr float CRUMB_PADDING = 6.0f;
static constexpr float CRUMB_GAP = 2.0f;
static constexpr float CRUMB_SEPARATOR_SIZE = 10.0f;

static constexpr float SECTION_HEADER_HEIGHT = 28.0f;
static constexpr float SECTION_PADDING = 10.0f;
static constexpr float SECTION_CHEVRON_SIZE = 10.0f;
static constexpr float SECTION_GAP = 8.0f;
static constexpr float SECTION_AFTER_TREE_GAP = 4.0f;
static constexpr float FOLDER_ROW_HEIGHT = 24.0f;
static constexpr float FOLDER_INDENT = 14.0f;
static constexpr float FOLDER_EXPANDER_SIZE = 12.0f;
static constexpr float FOLDER_EXPANDER_PADDING = 4.0f;
static constexpr float FOLDER_ICON_SIZE = 14.0f;
static constexpr float FOLDER_ICON_GAP = 6.0f;

static constexpr float CONTENT_PADDING = 12.0f;
static constexpr float CARD_GAP = 10.0f;
static constexpr float CARD_WIDTH = 124.0f;
static constexpr float CARD_HEIGHT = 150.0f;
static constexpr float CARD_MAX_WIDTH = 156.0f;
static constexpr float CARD_FOOTER_HEIGHT = 44.0f;
static constexpr float CARD_TYPEBAR_HEIGHT = 3.0f;
static constexpr float CARD_ICON_SIZE = 38.0f;
static constexpr float CARD_TEXT_INSET = 6.0f;
static constexpr float CARD_NAME_HEIGHT = 16.0f;
static constexpr float CARD_KIND_HEIGHT = 13.0f;
static constexpr float CARD_NAME_KIND_GAP = 1.0f;

static constexpr float ADD_MENU_WIDTH = 240.0f;

static constexpr float ASSET_TOOLTIP_WIDTH = 340.0f;
static constexpr float ASSET_TOOLTIP_PADDING = 10.0f;
static constexpr float ASSET_TOOLTIP_PREVIEW_SIZE = 48.0f;
static constexpr float ASSET_TOOLTIP_PREVIEW_ICON_SIZE = 24.0f;
static constexpr float ASSET_TOOLTIP_HEAD_HEIGHT = ASSET_TOOLTIP_PREVIEW_SIZE + 2.0f * ASSET_TOOLTIP_PADDING;
static constexpr float ASSET_TOOLTIP_TITLE_HEIGHT = 18.0f;
static constexpr float ASSET_TOOLTIP_CHIP_HEIGHT = 14.0f;
static constexpr float ASSET_TOOLTIP_CHIP_GAP = 4.0f;
static constexpr float ASSET_TOOLTIP_DOT_SIZE = 8.0f;
static constexpr float ASSET_TOOLTIP_DOT_GAP = 5.0f;
static constexpr float ASSET_TOOLTIP_DIVIDER_HEIGHT = 1.0f;
static constexpr float ASSET_TOOLTIP_FACTS_TOP = 8.0f;
static constexpr float ASSET_TOOLTIP_FACT_HEIGHT = 16.0f;
static constexpr float ASSET_TOOLTIP_FACT_GAP = 3.0f;
static constexpr float ASSET_TOOLTIP_KEY_WIDTH = 92.0f;
static constexpr float ASSET_TOOLTIP_KEY_VALUE_GAP = 10.0f;

using MenuItems = std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>>;

/**
 * @brief One line of an asset tooltip's facts
 */
struct AssetTooltipFact {
    std::string key;
    std::string value;
    bool monospace;
};

/**
 * @brief What an asset tooltip shows about the card it belongs to
 */
struct AssetTooltipContent {
    std::string title;
    std::string typeLabel;
    std::string iconSvg;
    std::string iconClass;
    std::optional<Amethyst::Color3> typeColor;
    std::vector<AssetTooltipFact> facts;
};

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
static void s_setGuiStateFlag(Amethyst::UIObject &object, uint16_t flag, bool on)
{
    const uint16_t state = object.getGuiState();
    object.setGuiState(on ? static_cast<uint16_t>(state | flag) : static_cast<uint16_t>(state & ~flag));
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
    std::string extension(Rapture::AssetRegistry::fileExtension(type));
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


static uint32_t s_countShownTreeRows(const Amethyst::TreeView &tree)
{
    uint32_t shown = 0;
    uint16_t foldedDepth = UINT16_MAX;
    for (uint32_t row = 0; row < tree.rowCount(); row++) {
        const uint16_t depth = tree.depth(row);
        if (depth > foldedDepth) {
            continue;
        }
        foldedDepth = UINT16_MAX;
        shown++;
        if (tree.hasChildren(row) && !tree.isExpanded(row)) {
            foldedDepth = depth;
        }
    }
    return shown;
}

static Amethyst::TextLabel *s_addAssetTooltipLabel(Amethyst::UIScope &scope, std::string_view styleClass, std::string text,
                                                   const Amethyst::UDim2 &position, const Amethyst::UDim2 &size)
{
    Amethyst::TextLabel *label = nullptr;
    scope.textLabel(
        {
            .classes = {std::string(styleClass)},
            .base = {.interactable = false, .position = position, .size = size},
            .label = std::move(text),
        },
        [&label](Amethyst::TextLabelScope &tl) { label = &tl.component; });
    return label;
}

static void s_fillAssetTooltipHead(Amethyst::UIScope &scope, const AssetTooltipContent &content)
{
    scope.frame(
        {
            .classes = {"asset-tooltip-preview"},
            .base =
                {
                    .interactable = false,
                    .position = Amethyst::UDim2::fromOffset(ASSET_TOOLTIP_PADDING, ASSET_TOOLTIP_PADDING),
                    .size = Amethyst::UDim2::fromOffset(ASSET_TOOLTIP_PREVIEW_SIZE, ASSET_TOOLTIP_PREVIEW_SIZE),
                },
        },
        [&content](Amethyst::FrameScope &preview) {
            preview.imageLabel({
                .classes = {content.iconClass},
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
                        .interactable = false,
                        .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
                        .size = Amethyst::UDim2::fromOffset(ASSET_TOOLTIP_PREVIEW_ICON_SIZE, ASSET_TOOLTIP_PREVIEW_ICON_SIZE),
                    },
                .svg = content.iconSvg,
            });
        });

    const float textLeft = 2.0f * ASSET_TOOLTIP_PADDING + ASSET_TOOLTIP_PREVIEW_SIZE;
    const float textTop =
        (ASSET_TOOLTIP_HEAD_HEIGHT - ASSET_TOOLTIP_TITLE_HEIGHT - ASSET_TOOLTIP_CHIP_GAP - ASSET_TOOLTIP_CHIP_HEIGHT) * 0.5f;
    s_addAssetTooltipLabel(scope, "asset-tooltip-title", content.title, Amethyst::UDim2::fromOffset(textLeft, textTop),
                           Amethyst::UDim2(1.0f, -(textLeft + ASSET_TOOLTIP_PADDING), 0.0f, ASSET_TOOLTIP_TITLE_HEIGHT));

    const float chipTop = textTop + ASSET_TOOLTIP_TITLE_HEIGHT + ASSET_TOOLTIP_CHIP_GAP;
    float chipTextLeft = textLeft;
    if (content.typeColor.has_value()) {
        scope.frame({
            .classes = {"asset-tooltip-dot"},
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                    .interactable = false,
                    .position = Amethyst::UDim2::fromOffset(textLeft, chipTop + ASSET_TOOLTIP_CHIP_HEIGHT * 0.5f),
                    .size = Amethyst::UDim2::fromOffset(ASSET_TOOLTIP_DOT_SIZE, ASSET_TOOLTIP_DOT_SIZE),
                },
            .style = {.backgroundColor = *content.typeColor},
        });
        chipTextLeft += ASSET_TOOLTIP_DOT_SIZE + ASSET_TOOLTIP_DOT_GAP;
    }
    Amethyst::TextLabel *chip = s_addAssetTooltipLabel(scope, "asset-tooltip-chip", content.typeLabel,
                                                       Amethyst::UDim2::fromOffset(chipTextLeft, chipTop),
                                                       Amethyst::UDim2::fromOffset(0.0f, ASSET_TOOLTIP_CHIP_HEIGHT));
    chip->setBaseProperties({.automaticSize = Amethyst::AutomaticSize::X});

    scope.frame({
        .classes = {"asset-tooltip-divider"},
        .base =
            {
                .interactable = false,
                .position = Amethyst::UDim2::fromOffset(0.0f, ASSET_TOOLTIP_HEAD_HEIGHT),
                .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ASSET_TOOLTIP_DIVIDER_HEIGHT),
            },
    });
}

static void s_fillAssetTooltip(Amethyst::Tooltip &surface, const AssetTooltipContent &content)
{
    surface.removeAllChildren();
    surface.setClasses({"tooltip"});
    surface.setBaseProperties({
        .automaticSize = Amethyst::AutomaticSize::OFF,
        .clipsDescendants = true,
        .padding = Amethyst::UDim4::fromOffset(0.0f),
    });

    Amethyst::UIScope scope(surface);
    s_fillAssetTooltipHead(scope, content);

    const float valueLeft = ASSET_TOOLTIP_PADDING + ASSET_TOOLTIP_KEY_WIDTH + ASSET_TOOLTIP_KEY_VALUE_GAP;
    float factTop = ASSET_TOOLTIP_HEAD_HEIGHT + ASSET_TOOLTIP_DIVIDER_HEIGHT + ASSET_TOOLTIP_FACTS_TOP;
    for (const AssetTooltipFact &fact : content.facts) {
        s_addAssetTooltipLabel(scope, "asset-tooltip-fact-key", fact.key, Amethyst::UDim2::fromOffset(ASSET_TOOLTIP_PADDING, factTop),
                               Amethyst::UDim2::fromOffset(ASSET_TOOLTIP_KEY_WIDTH, ASSET_TOOLTIP_FACT_HEIGHT));
        s_addAssetTooltipLabel(scope, fact.monospace ? "asset-tooltip-fact-value-mono" : "asset-tooltip-fact-value", fact.value,
                               Amethyst::UDim2::fromOffset(valueLeft, factTop),
                               Amethyst::UDim2(1.0f, -(valueLeft + ASSET_TOOLTIP_PADDING), 0.0f, ASSET_TOOLTIP_FACT_HEIGHT));
        factTop += ASSET_TOOLTIP_FACT_HEIGHT + ASSET_TOOLTIP_FACT_GAP;
    }

    const float contentHeight = factTop - ASSET_TOOLTIP_FACT_GAP + ASSET_TOOLTIP_PADDING;
    const Amethyst::vec4 insets = surface.resolveContentInsets(surface.absoluteSize);
    surface.setBaseProperties({.size = Amethyst::UDim2::fromOffset(ASSET_TOOLTIP_WIDTH, contentHeight + insets.x + insets.z)});
}

static AssetTooltipContent s_describeEntryForTooltip(const std::filesystem::directory_entry &entry, const std::string &displayName,
                                                     const Rapture::AssetMetadata *metadata, Rapture::AssetHandle handle)
{
    const std::filesystem::path projectDirectory = Rapture::Application::getInstance().getProject().getProjectDirectory();

    AssetTooltipContent content;
    content.title = displayName;
    content.facts.push_back({.key = "Location", .value = entry.path().lexically_relative(projectDirectory).generic_string(), .monospace = true});

    if (entry.is_directory()) {
        content.typeLabel = "Folder";
        content.iconSvg = Icons::SVG_FOLDER;
        content.iconClass = "content-browser-card-icon-folder";
        return content;
    }

    std::error_code sizeEc;
    const uintmax_t bytes = std::filesystem::file_size(entry.path(), sizeEc);
    if (!sizeEc) {
        content.facts.push_back({.key = "Size on disk", .value = Rapture::StringFormat_bytesToUnitString(bytes), .monospace = false});
    }

    content.iconClass = "content-browser-card-icon";
    if (metadata == nullptr) {
        const std::string extension = entry.path().extension().string();
        content.typeLabel = extension.empty() ? "File" : extension;
        content.iconSvg = Icons::SVG_SCRIPT;
        return content;
    }

    content.typeLabel = Asset_labelForType(metadata->assetType, metadata->authoredClass);
    content.iconSvg = Asset_iconForType(metadata->assetType, metadata->authoredClass);
    content.typeColor = Asset_colorForType(metadata->assetType);
    content.facts.push_back({.key = "Id", .value = std::format("{:016x}", handle), .monospace = true});
    return content;
}

ContentBrowserPanel::ContentBrowserPanel(const WorkspaceContext &context, std::function<void()> onDockRequested)
    : Panel("Content Browser", context), m_onDockRequested(std::move(onDockRequested))
{
    m_scene = context.world != nullptr ? context.world->getScene() : nullptr;
}

void ContentBrowserPanel::setContext(const WorkspaceContext &context)
{
    Panel::setContext(context);
    m_scene = context.world != nullptr ? context.world->getScene() : nullptr;
}

void ContentBrowserPanel::build(Amethyst::UIScope &scope)
{
    (void)scope;

    m_baseDirectory = Rapture::Application::getInstance().getProject().getContentDirectory();
    m_currentDirectory = m_baseDirectory;
    m_navigationHistory.push_back(m_currentDirectory);

    setupTopBar();
    setupSideBar();
    setupContentArea();
    setupContextMenu();

    buildDirectoryTree();
    refresh();
}

Amethyst::Frame *ContentBrowserPanel::addToolbarIsland()
{
    Amethyst::Frame *island = nullptr;
    Amethyst::UIScope(*m_topBarPane).frame(
        {
            .classes = {"content-browser-toolbar"},
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::X,
                    .clipsDescendants = true,
                    .layoutOrder = static_cast<uint32_t>(m_topBarPane->getChildren().size()),
                    .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(ISLAND_PADDING), .left = Amethyst::UDim::fromOffset(ISLAND_PADDING)},
                    .size = Amethyst::UDim2::fromScale(0.0f, 1.0f),
                },
        },
        [&island](Amethyst::FrameScope &f) { island = &f.component; });
    return island;
}

Amethyst::Frame *ContentBrowserPanel::addToolbarSeparator()
{
    Amethyst::Frame *separator = nullptr;
    Amethyst::UIScope(*m_topBarPane).frame(
        {
            .classes = {"content-browser-separator"},
            .base =
                {
                    .layoutOrder = static_cast<uint32_t>(m_topBarPane->getChildren().size()),
                    .size = Amethyst::UDim2(0.0f, SEPARATOR_WIDTH, 1.0f, 0.0f),
                },
        },
        [&separator](Amethyst::FrameScope &f) { separator = &f.component; });
    return separator;
}

static void s_placeToolbarControl(Amethyst::UIObject &control, float left, const Amethyst::UDim2 &size)
{
    control.setBaseProperties({
        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
        .position = Amethyst::UDim2(0.0f, left, 0.5f, 0.0f),
        .size = size,
    });
}

IconLabelButton *ContentBrowserPanel::addToolbarButtonIsland(std::string_view iconSvg, std::string_view label, std::string_view styleClass)
{
    auto *button = addToolbarIsland()->add<IconLabelButton>(iconSvg, label, std::vector<std::string>{std::string(styleClass)});
    s_placeToolbarControl(*button, 0.0f, Amethyst::UDim2::fromOffset(0.0f, TOOLBAR_CONTROL_HEIGHT));
    return button;
}

void ContentBrowserPanel::setupTopBar()
{
    const EditorContext &editor = *m_context.editor;

    Amethyst::UIScope(*m_root).frame(
        {
            .classes = {"content-browser-toolbar"},
            .base = {.clipsDescendants = true, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TOOLBAR_HEIGHT)},
        },
        [this](Amethyst::FrameScope &top) { m_topBarPane = &top.component; });
    auto *islands = m_topBarPane->addExtension<Amethyst::UIListLayout>();
    islands->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    islands->horizontalFlex = Amethyst::UiFlexAlignment::FILL;

    Amethyst::UIScope(*m_root).frame({
        .classes = {"content-browser-separator"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(0.0f, TOOLBAR_HEIGHT),
                .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, SEPARATOR_WIDTH),
            },
    });

    m_addBtn = addToolbarButtonIsland(Icons::SVG_PLUS, "Add", "hotbar-primary-button");
    m_addBtn->track(m_addBtn->onMouseButton1ClickCb.connect(
        [this]() { showAddMenu({m_addBtn->absolutePosition.x, m_addBtn->absolutePosition.y}); }));
    ControlTooltip_attach(*m_addBtn, CONTENT_BROWSER_TOOLTIPS[CONTENT_BROWSER_TOOLTIP_ADD], editor);
    addToolbarSeparator();

    m_importBtn = addToolbarButtonIsland(Icons::SVG_FOLDER_PLUS, "Import", "hotbar-button");
    m_importBtn->track(m_importBtn->onMouseButton1ClickCb.connect([this]() {
        WindowManager *windows = m_context.editor->windows;
        windows->openFileDialog(FILE_DIALOG_OPEN,
                                [this, windows](const std::filesystem::path &path) { windows->openImportPanel(path, m_currentDirectory); });
    }));
    ControlTooltip_attach(*m_importBtn, CONTENT_BROWSER_TOOLTIPS[CONTENT_BROWSER_TOOLTIP_IMPORT], editor);
    addToolbarSeparator();

    Amethyst::Frame *navIsland = addToolbarIsland();
    const std::vector<std::string> navClasses = {"content-browser-nav-button"};
    const Amethyst::UDim2 navButtonSize = Amethyst::UDim2::fromOffset(NAV_BUTTON_WIDTH, TOOLBAR_CONTROL_HEIGHT);
    m_goBackBtn = navIsland->add<IconButton>(Icons::SVG_NAV_BACK, NAV_ICON_SIZE, navClasses);
    m_goForwardBtn = navIsland->add<IconButton>(Icons::SVG_NAV_FORWARD, NAV_ICON_SIZE, navClasses);
    m_goUpBtn = navIsland->add<IconButton>(Icons::SVG_NAV_UP, NAV_ICON_SIZE, navClasses);
    s_placeToolbarControl(*m_goBackBtn, 0.0f, navButtonSize);
    s_placeToolbarControl(*m_goForwardBtn, NAV_BUTTON_WIDTH + NAV_BUTTON_GAP, navButtonSize);
    s_placeToolbarControl(*m_goUpBtn, 2.0f * (NAV_BUTTON_WIDTH + NAV_BUTTON_GAP), navButtonSize);
    m_goBackBtn->track(m_goBackBtn->onMouseButton1ClickCb.connect([this]() { navigateBack(); }));
    m_goForwardBtn->track(m_goForwardBtn->onMouseButton1ClickCb.connect([this]() { navigateForward(); }));
    m_goUpBtn->track(m_goUpBtn->onMouseButton1ClickCb.connect([this]() { navigateUp(); }));
    ControlTooltip_attach(*m_goBackBtn, CONTENT_BROWSER_TOOLTIPS[CONTENT_BROWSER_TOOLTIP_BACK], editor);
    ControlTooltip_attach(*m_goForwardBtn, CONTENT_BROWSER_TOOLTIPS[CONTENT_BROWSER_TOOLTIP_FORWARD], editor);
    ControlTooltip_attach(*m_goUpBtn, CONTENT_BROWSER_TOOLTIPS[CONTENT_BROWSER_TOOLTIP_UP], editor);
    addToolbarSeparator();

    // the breadcrumb takes what the other islands leave, sharing it with the search field within its limits
    Amethyst::Frame *breadcrumbIsland = addToolbarIsland();
    breadcrumbIsland->setBaseProperties({.automaticSize = Amethyst::AutomaticSize::OFF});
    breadcrumbIsland->addExtension<Amethyst::UIFlexItem>();
    Amethyst::UIScope(*breadcrumbIsland).frame(
        {
            .base = {.clipsDescendants = true, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &crumbs) { m_breadcrumbBar = &crumbs.component; });
    auto *crumbs = m_breadcrumbBar->addExtension<Amethyst::UIListLayout>();
    crumbs->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    crumbs->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    crumbs->innerPadding = Amethyst::UDim::fromOffset(CRUMB_GAP);
    addToolbarSeparator();

    Amethyst::Frame *searchIsland = addToolbarIsland();
    searchIsland->setBaseProperties({
        .automaticSize = Amethyst::AutomaticSize::OFF,
        .size = Amethyst::UDim2(0.0f, SEARCH_FIELD_MAX_WIDTH + 2.0f * ISLAND_PADDING, 1.0f, 0.0f),
    });
    searchIsland->addExtension<Amethyst::UIFlexItem>();
    auto *searchLimits = searchIsland->addExtension<Amethyst::UISizeConstraint>();
    searchLimits->minSize = Amethyst::vec2(SEARCH_FIELD_MIN_WIDTH + 2.0f * ISLAND_PADDING, 0.0f);
    searchLimits->maxSize = Amethyst::vec2(SEARCH_FIELD_MAX_WIDTH + 2.0f * ISLAND_PADDING, std::numeric_limits<float>::max());
    m_searchField = searchIsland->add<SearchField>("Search content");
    s_placeToolbarControl(*m_searchField, 0.0f, Amethyst::UDim2(1.0f, 0.0f, 0.0f, SEARCH_FIELD_HEIGHT));
    m_searchField->setOnTextChanged([this](const std::string &text) { onSearchTextChanged(text); });
    addToolbarSeparator();

    if (m_onDockRequested) {
        IconLabelButton *dockButton = addToolbarButtonIsland(Icons::SVG_PIN, "Dock", "hotbar-button");
        Amethyst::Frame *dockSeparator = addToolbarSeparator();
        dockButton->track(dockButton->onMouseButton1ClickCb.connect([this, dockButton, dockSeparator]() {
            // the browser lives in the layout from here on, so the island offering to put it there goes
            dockButton->parent->asUiObject()->setBaseProperties({.visible = false});
            dockSeparator->setBaseProperties({.visible = false});
            std::function<void()> dock = std::move(m_onDockRequested);
            dock();
        }));
        ControlTooltip_attach(*dockButton, CONTENT_BROWSER_TOOLTIPS[CONTENT_BROWSER_TOOLTIP_DOCK], editor);
    }

    m_settingsBtn = addToolbarIsland()->add<IconButton>(Icons::SVG_SETTINGS, SETTINGS_ICON_SIZE, navClasses);
    s_placeToolbarControl(*m_settingsBtn, 0.0f, navButtonSize);
    ControlTooltip_attach(*m_settingsBtn, CONTENT_BROWSER_TOOLTIPS[CONTENT_BROWSER_TOOLTIP_SETTINGS], editor);
}

void ContentBrowserPanel::setupSideBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .classes = {"content-browser-sidebar"},
            .base =
                {
                    .clipsDescendants = true,
                    .position = Amethyst::UDim2::fromOffset(0.0f, BODY_TOP),
                    .size = Amethyst::UDim2(0.0f, SIDE_BAR_WIDTH, 1.0f, -BODY_TOP),
                },
        },
        [this](Amethyst::FrameScope &side) { m_sideBarPane = &side.component; });
    auto *sections = m_sideBarPane->addExtension<Amethyst::UIListLayout>();
    sections->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;

    m_projectSection.expanded = true;
    buildSidebarSection(m_projectSection, "PROJECT", 0);

    // as tall as its shown rows until that outgrows the sidebar, then it scrolls them itself
    Amethyst::UIScope(*m_sideBarPane)
        .treeView(
            {
                .classes = {"content-browser-folder-tree"},
                .base =
                    {
                        .layoutOrder = 1,
                        .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -(2.0f * SECTION_HEADER_HEIGHT + SECTION_AFTER_TREE_GAP)),
                    },
                .treeView =
                    {
                        .rowHeight = FOLDER_ROW_HEIGHT,
                        .cellPadding = Amethyst::UDim4{{0.0f, 0.0f}},
                        .showColumnSeparators = false,
                        .disclosureTriangleSize = FOLDER_EXPANDER_SIZE,
                        .disclosureTrianglePadding = FOLDER_EXPANDER_PADDING,
                        .indentPerLevel = FOLDER_INDENT,
                        .fillRows = false,
                        .showHeader = false,
                    },
            },
            [this](Amethyst::TreeViewScope &tv) {
                m_directoryTree = &tv.component;
                tv.column("", 1.0f);
            });
    m_directoryTreeLimits = m_directoryTree->addExtension<Amethyst::UISizeConstraint>();
    m_directoryTreeLimits->maxSize = Amethyst::vec2(std::numeric_limits<float>::max(), 0.0f);
    m_directoryTree->onRowClicked = [this](uint32_t row) {
        if (row < m_directoryRowPaths.size()) {
            navigateToDirectory(m_directoryRowPaths[row]);
        }
    };
    m_directoryTree->onRowToggled = [this](uint32_t row, bool expanded) {
        (void)row;
        (void)expanded;
        layoutSideBar();
    };

    Amethyst::UIScope(*m_sideBarPane)
        .frame({
            .base = {.interactable = false, .layoutOrder = 2, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, SECTION_AFTER_TREE_GAP)},
            .style = {.backgroundTransparency = 1.0f},
        });

    m_recentSection.expanded = false;
    buildSidebarSection(m_recentSection, "RECENT", 3);

    Amethyst::UIScope(*m_root).frame({
        .classes = {"content-browser-separator"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(SIDE_BAR_WIDTH, BODY_TOP),
                .size = Amethyst::UDim2(0.0f, SEPARATOR_WIDTH, 1.0f, -BODY_TOP),
            },
    });

    layoutSideBar();
}

void ContentBrowserPanel::buildSidebarSection(SidebarSection &section, std::string_view title, uint32_t layoutOrder)
{
    section.header = m_sideBarPane->add<Amethyst::InvisibleButton>();
    section.header->setBaseProperties({
        .layoutOrder = layoutOrder,
        .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(SECTION_PADDING), .left = Amethyst::UDim::fromOffset(SECTION_PADDING)},
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, SECTION_HEADER_HEIGHT),
    });
    // the line after the title takes the rest of the row
    auto *row = section.header->addExtension<Amethyst::UIListLayout>();
    row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    row->horizontalFlex = Amethyst::UiFlexAlignment::FILL;
    row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    row->innerPadding = Amethyst::UDim::fromOffset(SECTION_GAP);

    Amethyst::UIScope scope(*section.header);
    scope.imageLabel(
        {
            .classes = {"content-browser-section-chevron"},
            .base =
                {
                    .interactable = false,
                    .layoutOrder = 0,
                    .size = Amethyst::UDim2::fromOffset(SECTION_CHEVRON_SIZE, SECTION_CHEVRON_SIZE),
                },
            .svg = section.expanded ? Icons::SVG_CHEVRON_DOWN : Icons::SVG_CARET_RIGHT,
        },
        [&section](Amethyst::ImageLabelScope &il) { section.chevron = &il.component; });
    scope.textLabel({
        .classes = {"content-browser-section-title"},
        .base =
            {
                .automaticSize = Amethyst::AutomaticSize::X,
                .interactable = false,
                .layoutOrder = 1,
                .size = Amethyst::UDim2::fromScale(0.0f, 1.0f),
            },
        .label = std::string(title),
    });
    scope.frame(
        {
            .classes = {"content-browser-section-line"},
            .base =
                {
                    .interactable = false,
                    .layoutOrder = 2,
                    .size = Amethyst::UDim2::fromOffset(0.0f, 1.0f),
                },
        },
        [](Amethyst::FrameScope &line) { line.component.addExtension<Amethyst::UIFlexItem>(); });

    section.header->track(section.header->onMouseButton1ClickCb.connect([this, &section]() {
        section.expanded = !section.expanded;
        section.chevron->setSvg(section.expanded ? Icons::SVG_CHEVRON_DOWN : Icons::SVG_CARET_RIGHT);
        layoutSideBar();
    }));
}

void ContentBrowserPanel::layoutSideBar()
{
    const float shownRowsHeight = static_cast<float>(s_countShownTreeRows(*m_directoryTree)) * FOLDER_ROW_HEIGHT;
    m_directoryTreeLimits->maxSize.y = shownRowsHeight;
    m_directoryTree->setBaseProperties({.visible = m_projectSection.expanded});
    m_directoryTree->markDirty();
    m_sideBarPane->markDirty();
}

void ContentBrowserPanel::setupContentArea()
{
    Amethyst::UIScope rootScope(*m_root);
    rootScope.scrollingFrame(
        {
            .classes = {"grid-sink"},
            .base =
                {
                    .clipsDescendants = true,
                    .padding = Amethyst::UDim4::fromOffset(CONTENT_PADDING),
                    .position = Amethyst::UDim2::fromOffset(CONTENT_LEFT, BODY_TOP),
                    .size = Amethyst::UDim2(1.0f, -CONTENT_LEFT, 1.0f, -(BODY_TOP + SEPARATOR_WIDTH + STATUS_BAR_HEIGHT)),
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
            gridLayout->cellSize = Amethyst::UDim2::fromOffset(CARD_WIDTH, CARD_HEIGHT);
            gridLayout->cellPadding = Amethyst::UDim2::fromOffset(CARD_GAP, CARD_GAP);
            gridLayout->flexCells = true;
            gridLayout->maxCellWidth = CARD_MAX_WIDTH;
            gridLayout->cellAspectRatio = CARD_WIDTH / CARD_HEIGHT;
            gridLayout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
            gridLayout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
            gridLayout->startCorner = Amethyst::StartCorner::TOP_LEFT;
        });

    rootScope.frame({
        .classes = {"content-browser-separator"},
        .base =
            {
                .position = Amethyst::UDim2(0.0f, CONTENT_LEFT, 1.0f, -(SEPARATOR_WIDTH + STATUS_BAR_HEIGHT)),
                .size = Amethyst::UDim2(1.0f, -CONTENT_LEFT, 0.0f, SEPARATOR_WIDTH),
            },
    });

    rootScope.textLabel(
        {
            .classes = {"content-browser-status"},
            .base =
                {
                    .padding = Amethyst::UDim4{.left = Amethyst::UDim::fromOffset(CONTENT_PADDING)},
                    .position = Amethyst::UDim2(0.0f, CONTENT_LEFT, 1.0f, -STATUS_BAR_HEIGHT),
                    .size = Amethyst::UDim2(1.0f, -CONTENT_LEFT, 0.0f, STATUS_BAR_HEIGHT),
                },
            .label = "0 items",
        },
        [this](Amethyst::TextLabelScope &lbl) { m_statusLabel = &lbl.component; });
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
        .action = [] { return std::make_unique<AddAssetMenuItemView>(); },
        .separator = [] { return std::make_unique<SeparatorMenuItemView>(); },
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

    items.push_back(SeparatorMenuItem::create("Folder"));
    items.push_back(
        AddAssetMenuItem::createIconRow("New Folder", Icons::SVG_FOLDER_PLUS, [this]() { beginCreate(CONTENT_EDIT_FOLDER); }));
    items.push_back(AddAssetMenuItem::createIconRow("Refresh", Icons::SVG_REFRESH, [this]() { refresh(); }));

    items.push_back(SeparatorMenuItem::create("Create Basic Asset"));
    items.push_back(AddAssetMenuItem::createAssetRow("Material", Rapture::ASSET_MATERIAL, []() { s_createAssetStub("material"); }));
    items.push_back(AddAssetMenuItem::createAssetRow("World", Rapture::ASSET_WORLD, []() { s_createAssetStub("world"); }));
    items.push_back(
        AddAssetMenuItem::createAssetRow("Script", Rapture::ASSET_SCRIPT, [this]() { beginCreate(CONTENT_EDIT_SCRIPT); }));

    items.push_back(SeparatorMenuItem::create("Create Advanced Asset"));
    for (auto &advanced : buildAdvancedAddItems()) {
        items.push_back(std::move(advanced));
    }

    return items;
}

MenuItems ContentBrowserPanel::buildAdvancedAddItems()
{
    // a row is labelled with the class it creates, so a class is named once and never relabelled
    auto appendClass = [this](MenuItems &into, const Rapture::TypeInfo &type) {
        into.push_back(
            AddAssetMenuItem::createPlainRow(std::string(type.name), [this, &type]() { beginCreate(CONTENT_EDIT_MODULE, &type); }));
    };

    MenuItems controllers;
    appendClass(controllers, Rapture::CameraController::staticType());
    appendClass(controllers, Rapture::PlayerController::staticType());

    MenuItems lights;
    appendClass(lights, Rapture::DirectionalLight3D::staticType());
    appendClass(lights, Rapture::PointLight3D::staticType());
    appendClass(lights, Rapture::SpotLight3D::staticType());

    MenuItems scenes;
    scenes.push_back(AddAssetMenuItem::createPlainRow("Level", []() { s_createAssetStub("level"); }));
    appendClass(scenes, Rapture::StaticMesh3D::staticType());
    appendClass(scenes, Rapture::Camera3D::staticType());
    appendClass(scenes, Rapture::SpringArm3D::staticType());
    scenes.push_back(Amethyst::makeSubmenuItem("Lights", std::move(lights)));
    scenes.push_back(Amethyst::makeSubmenuItem("Controllers", std::move(controllers)));

    MenuItems shaders;
    shaders.push_back(AddAssetMenuItem::createPlainRow("Compute Shader", []() { s_createAssetStub("compute shader"); }));
    shaders.push_back(AddAssetMenuItem::createPlainRow("Graphics Shader", []() { s_createAssetStub("graphics shader"); }));

    MenuItems textures;
    textures.push_back(AddAssetMenuItem::createPlainRow("Texture", []() { s_createAssetStub("texture"); }));
    textures.push_back(AddAssetMenuItem::createPlainRow("Cubemap", []() { s_createAssetStub("cubemap"); }));

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

    switch (kind) {
    case CONTENT_EDIT_FOLDER:
        m_edit.initialName = s_uniqueFolderName(m_currentDirectory);
        break;
    case CONTENT_EDIT_MODULE:
        m_edit.initialName = s_uniqueAssetName(m_currentDirectory, objectClass->name, Rapture::ASSET_MODULE);
        break;
    case CONTENT_EDIT_SCRIPT:
        m_edit.initialName = s_uniqueAssetName(m_currentDirectory, "Script", Rapture::ASSET_SCRIPT);
        break;
    default:
        break;
    }

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
            .position = item.name->getBaseProperties().position,
            .size = item.name->getBaseProperties().size,
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
        case CONTENT_EDIT_SCRIPT:
            createScript(name);
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
    root->serializeSubtree(document->root());
    document->freeze();

    Rapture::AssetImportDataRequest request;
    request.data = Rapture::ModuleImportData{std::move(document)};
    request.output = m_currentDirectory;
    request.name = std::string(name);

    if (!Rapture::AssetManager::importAsset(std::move(request))) {
        RP_ERROR("Could not create a '{}' named '{}' in '{}'", type.name, name, m_currentDirectory.string());
    }
}

void ContentBrowserPanel::createScript(std::string_view name)
{
    Rapture::AssetHandle script = ScriptingWorkspace::createScript(m_currentDirectory, name);
    if (script == Rapture::INVALID_ASSET_HANDLE) {
        return;
    }

    m_context.editor->workspaces->openAsset(script);
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
    return type == Rapture::ASSET_MODULE || type == Rapture::ASSET_MATERIAL_INSTANCE || type == Rapture::ASSET_STATIC_MESH ||
           type == Rapture::ASSET_SKELETAL_MESH || type == Rapture::ASSET_SKELETON;
}

std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> ContentBrowserPanel::assetActions(Rapture::AssetType type,
                                                                                                Rapture::AssetHandle handle)
{
    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;

    if (s_opensInWorkspace(type)) {
        items.push_back(Amethyst::makeActionItem("Open", [this, handle]() { m_context.editor->workspaces->openAsset(handle); }));
    }

    switch (type) {
    case Rapture::ASSET_MODULE:
        items.push_back(Amethyst::makeActionItem("Load in scene", [this, handle]() { s_spawnModule(handle, m_scene); }));
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
    segments.emplace_back(m_baseDirectory.filename().string(), m_baseDirectory);

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

    Amethyst::UIScope scope(*m_breadcrumbBar);
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) {
            scope.imageLabel({
                .classes = {"content-browser-crumb-separator"},
                .base =
                    {
                        .interactable = false,
                        .layoutOrder = static_cast<uint32_t>(m_breadcrumbBar->getChildren().size()),
                        .size = Amethyst::UDim2::fromOffset(CRUMB_SEPARATOR_SIZE, CRUMB_SEPARATOR_SIZE),
                    },
                .svg = Icons::SVG_CARET_RIGHT,
            });
        }

        const std::filesystem::path &target = segments[i].second;
        const uint32_t layoutOrder = static_cast<uint32_t>(m_breadcrumbBar->getChildren().size());
        Amethyst::TextButton *crumb = nullptr;
        if (i == 0) {
            auto *rootCrumb = m_breadcrumbBar->add<IconLabelButton>(Icons::SVG_FOLDER, segments[i].first,
                                                                     std::vector<std::string>{"content-browser-crumb"});
            rootCrumb->getIcon().setClasses({"content-browser-crumb-icon"});
            crumb = rootCrumb;
        } else {
            scope.textButton(
                {
                    .classes = {"content-browser-crumb"},
                    .base =
                        {
                            .automaticSize = Amethyst::AutomaticSize::X,
                            .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(CRUMB_PADDING),
                                                       .left = Amethyst::UDim::fromOffset(CRUMB_PADDING)},
                        },
                    .label = segments[i].first,
                },
                [&crumb](Amethyst::TextButtonScope &b) { crumb = &b.component; });
        }
        crumb->setBaseProperties({.layoutOrder = layoutOrder, .size = Amethyst::UDim2::fromOffset(0.0f, CRUMB_HEIGHT)});
        crumb->track(crumb->onMouseButton1ClickCb.connect([this, target]() { navigateToDirectory(target); }));
        s_setGuiStateFlag(*crumb, Amethyst::GUI_STATE_ACTIVE, i + 1 == segments.size());
    }
}

void ContentBrowserPanel::refresh()
{
    m_selectedItem = SIZE_MAX;
    refreshFileBrowser();
    rebuildBreadcrumb();
    refreshNavigationButtons();
    selectCurrentDirectoryRow();
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

    m_shownEntryPaths.clear();

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
        if (!isDir && Rapture::AssetRegistry::isRaptureExtension(entry.path().extension().string())) {
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
        m_shownEntryPaths.push_back(entry.path());

        if (isDir) {
            item.icon->setSvg(Icons::SVG_FOLDER);
            item.icon->setClasses({"content-browser-card-icon-folder"});
            layoutCardFooter(item, "", std::nullopt);
        } else if (metadata != nullptr) {
            item.icon->setSvg(Asset_iconForType(metadata->assetType, metadata->authoredClass));
            item.icon->setClasses({"content-browser-card-icon"});
            layoutCardFooter(item, Asset_labelForType(metadata->assetType, metadata->authoredClass), Asset_colorForType(metadata->assetType));
        } else {
            item.icon->setSvg(Icons::SVG_SCRIPT);
            item.icon->setClasses({"content-browser-card-icon"});
            std::string extension = entry.path().extension().string();
            layoutCardFooter(item, extension.empty() ? "file" : extension, std::nullopt);
        }

        item.name->setText(displayName);

        item.tooltip->build = [content = s_describeEntryForTooltip(entry, displayName, metadata, assetHandle)](Amethyst::Tooltip &surface) {
            s_fillAssetTooltip(surface, content);
        };

        size_t itemIndex = index;
        item.clickConnection = item.action->onMouseButton1ClickCb.connect([this, itemIndex]() { selectItem(itemIndex); });

        if (isDir) {
            std::filesystem::path dirPath = entry.path();
            item.doubleClickConnection =
                item.action->onMouseButton1DoubleClickCb.connect([this, dirPath](int32_t x, int32_t y) {
                    (void)x;
                    (void)y;
                    navigateToDirectory(dirPath);
                });
        } else if (assetHandle != Rapture::INVALID_ASSET_HANDLE) {
            item.doubleClickConnection =
                item.action->onMouseButton1DoubleClickCb.connect([this, assetHandle](int32_t x, int32_t y) {
                    (void)x;
                    (void)y;
                    m_context.editor->workspaces->openAsset(assetHandle);
                });
        } else {
            item.doubleClickConnection.disconnect();
        }

        Rapture::AssetType assetType = metadata != nullptr ? metadata->assetType : Rapture::ASSET_NONE;
        bool isAsset = metadata != nullptr;
        std::filesystem::path itemPath = entry.path();
        item.contextMenuConnection = item.action->onMouseButton2DownCb.connect([this, isDir, isAsset, assetType, assetHandle,
                                                                                itemIndex, itemPath](int32_t x, int32_t y) {
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
            items.push_back(Amethyst::makeSeparatorItem());
            items.push_back(Amethyst::makeActionItem(
                "Copy Filename", [itemPath]() { Amethyst::InputInterface::setClipboardText(itemPath.filename().string()); }));
            items.push_back(Amethyst::makeActionItem("Copy Full Path", [itemPath]() {
                Amethyst::InputInterface::setClipboardText(std::filesystem::absolute(itemPath).string());
            }));
            showContextMenu(Amethyst::vec2(static_cast<float>(x), static_cast<float>(y)), std::move(items));
        });

        index++;
    }

    if (m_edit.kind == CONTENT_EDIT_FOLDER || m_edit.kind == CONTENT_EDIT_MODULE || m_edit.kind == CONTENT_EDIT_SCRIPT) {
        auto &item = acquirePoolItem(index);
        item.container->setBaseProperties({.layoutOrder = static_cast<uint32_t>(index)});
        item.clickConnection.disconnect();
        item.contextMenuConnection.disconnect();

        item.tooltip->build = nullptr;

        if (m_edit.kind == CONTENT_EDIT_FOLDER) {
            item.icon->setSvg(Icons::SVG_FOLDER);
            item.icon->setClasses({"content-browser-card-icon-folder"});
            layoutCardFooter(item, "", std::nullopt);
        } else {
            Rapture::AssetType assetType = m_edit.kind == CONTENT_EDIT_MODULE ? Rapture::ASSET_MODULE : Rapture::ASSET_SCRIPT;
            item.icon->setSvg(Asset_iconForType(assetType, m_edit.objectClass));
            item.icon->setClasses({"content-browser-card-icon"});
            layoutCardFooter(item, Asset_labelForType(assetType, m_edit.objectClass), Asset_colorForType(assetType));
        }

        m_edit.itemIndex = index;
        startEditing(item, m_edit.initialName);
        index++;
    }

    releasePoolItems(index);
    updateStatus(index);
}

void ContentBrowserPanel::layoutCardFooter(ContentItemComponents &item, std::string_view kind, std::optional<Amethyst::Color3> typeColor)
{
    const float textAreaHeight = CARD_FOOTER_HEIGHT - CARD_TYPEBAR_HEIGHT;
    const Amethyst::UDim2 nameSize(1.0f, -2.0f * CARD_TEXT_INSET, 0.0f, CARD_NAME_HEIGHT);

    const bool showsKind = !kind.empty();
    float nameTop = (textAreaHeight - CARD_NAME_HEIGHT) * 0.5f;
    if (showsKind) {
        nameTop = (textAreaHeight - CARD_NAME_HEIGHT - CARD_NAME_KIND_GAP - CARD_KIND_HEIGHT) * 0.5f;
        item.type->setText(std::string(kind));
        item.type->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(CARD_TEXT_INSET, nameTop + CARD_NAME_HEIGHT + CARD_NAME_KIND_GAP),
            .size = Amethyst::UDim2(1.0f, -2.0f * CARD_TEXT_INSET, 0.0f, CARD_KIND_HEIGHT),
        });
    }
    item.type->setBaseProperties({.visible = showsKind});

    item.name->setBaseProperties({.position = Amethyst::UDim2::fromOffset(CARD_TEXT_INSET, nameTop), .size = nameSize});
    if (item.nameInput != nullptr) {
        item.nameInput->setBaseProperties({.position = Amethyst::UDim2::fromOffset(CARD_TEXT_INSET, nameTop), .size = nameSize});
    }

    item.typeBar->setBaseProperties({.visible = typeColor.has_value()});
    if (typeColor.has_value()) {
        item.typeBar->setBaseStyleProperties({.backgroundColor = *typeColor});
    }
}

void ContentBrowserPanel::updateStatus(size_t itemCount)
{
    if (m_statusLabel == nullptr) {
        return;
    }
    std::string text = std::to_string(itemCount) + (itemCount == 1 ? " item" : " items");
    if (m_selectedItem != SIZE_MAX) {
        text += ", 1 selected";
    }
    m_statusLabel->setText(text);
}

static std::unique_ptr<Amethyst::Frame> s_makeDirectoryCell(const std::string &label)
{
    auto cell = std::make_unique<Amethyst::Frame>();
    cell->setBaseProperties({.interactable = false, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
    cell->setBaseStyleProperties({.backgroundTransparency = 1.0f});

    Amethyst::UIScope scope(*cell);
    scope.imageLabel({
        .classes = {"treeview-icon-folder"},
        .base =
            {
                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                .interactable = false,
                .position = Amethyst::UDim2(0.0f, 0.0f, 0.5f, 0.0f),
                .size = Amethyst::UDim2::fromOffset(FOLDER_ICON_SIZE, FOLDER_ICON_SIZE),
            },
        .style = {.backgroundTransparency = 1.0f},
        .svg = Icons::SVG_FOLDER,
    });
    scope.textLabel({
        .classes = {"content-browser-folder-label"},
        .base =
            {
                .interactable = false,
                .position = Amethyst::UDim2::fromOffset(FOLDER_ICON_SIZE + FOLDER_ICON_GAP, 0.0f),
                .size = Amethyst::UDim2(1.0f, -(FOLDER_ICON_SIZE + FOLDER_ICON_GAP), 1.0f, 0.0f),
            },
        .label = label,
    });
    return cell;
}

void ContentBrowserPanel::buildDirectoryTree()
{
    m_directoryTree->clear();
    m_directoryRowPaths.clear();

    if (std::filesystem::exists(m_baseDirectory)) {
        addDirectoryRow(m_baseDirectory, 0);
        buildFilesSubtree(m_baseDirectory, 1);
    }

    layoutSideBar();
    selectCurrentDirectoryRow();
}

void ContentBrowserPanel::addDirectoryRow(const std::filesystem::path &path, uint16_t depth)
{
    m_directoryTree->addRow(depth);
    m_directoryTree->nextCell(s_makeDirectoryCell(path.filename().string()));
    m_directoryRowPaths.push_back(path);
}

void ContentBrowserPanel::buildFilesSubtree(const std::filesystem::path &path, uint16_t depth)
{
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_directory()) {
            continue;
        }

        addDirectoryRow(entry.path(), depth);
        buildFilesSubtree(entry.path(), static_cast<uint16_t>(depth + 1));
    }
}

void ContentBrowserPanel::selectCurrentDirectoryRow()
{
    if (m_directoryTree == nullptr) {
        return;
    }

    const auto found = std::ranges::find(m_directoryRowPaths, m_currentDirectory);
    m_directoryTree->selectedRow = found != m_directoryRowPaths.end() ? static_cast<int32_t>(found - m_directoryRowPaths.begin())
                                                                      : Amethyst::TreeView::NO_ROW_SELECTION;
    m_directoryTree->markDirty();
}

void ContentBrowserPanel::navigateToDirectory(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        return;
    }

    if (m_historyIndex < m_navigationHistory.size() - 1) {
        m_navigationHistory.erase(m_navigationHistory.begin() + static_cast<std::ptrdiff_t>(m_historyIndex) + 1, m_navigationHistory.end());
    }

    m_currentDirectory = path;
    m_navigationHistory.push_back(m_currentDirectory);
    m_historyIndex = m_navigationHistory.size() - 1;

    refresh();
}

void ContentBrowserPanel::revealAsset(Rapture::AssetHandle handle)
{
    const std::filesystem::path &assetPath = Rapture::AssetManager::getAssetMetadata(handle).assetPath;
    if (assetPath.empty()) {
        return;
    }

    navigateToDirectory(assetPath.parent_path());

    for (size_t index = 0; index < m_shownEntryPaths.size(); index++) {
        if (m_shownEntryPaths[index].filename() == assetPath.filename()) {
            selectItem(index);
            return;
        }
    }
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

void ContentBrowserPanel::navigateUp()
{
    if (m_currentDirectory != m_baseDirectory) {
        navigateToDirectory(m_currentDirectory.parent_path());
    }
}

void ContentBrowserPanel::refreshNavigationButtons()
{
    if (m_goBackBtn == nullptr) {
        return;
    }

    m_goBackBtn->setGuiStateFlag(Amethyst::GUI_STATE_DISABLED, m_historyIndex == 0);
    m_goForwardBtn->setGuiStateFlag(Amethyst::GUI_STATE_DISABLED, m_historyIndex + 1 >= m_navigationHistory.size());
    m_goUpBtn->setGuiStateFlag(Amethyst::GUI_STATE_DISABLED, m_currentDirectory == m_baseDirectory);
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

    if (m_selectedItem < m_contentItemPool.size()) {
        applyItemSelection(m_contentItemPool[m_selectedItem], false);
    }

    m_selectedItem = index;

    if (m_selectedItem < m_contentItemPool.size()) {
        applyItemSelection(m_contentItemPool[m_selectedItem], true);
    }

    size_t shownItems = 0;
    for (const ContentItemComponents &item : m_contentItemPool) {
        if (item.attached) {
            shownItems++;
        }
    }
    updateStatus(shownItems);
}

void ContentBrowserPanel::applyItemSelection(ContentItemComponents &item, bool selected)
{
    s_setGuiStateFlag(*item.container, Amethyst::GUI_STATE_ACTIVE, selected);
    s_setGuiStateFlag(*item.footer, Amethyst::GUI_STATE_ACTIVE, selected);
    s_setGuiStateFlag(*item.type, Amethyst::GUI_STATE_ACTIVE, selected);
}

void ContentBrowserPanel::applyItemHover(ContentItemComponents &item, bool hovered)
{
    s_setGuiStateFlag(*item.container, Amethyst::GUI_STATE_HOVERED, hovered);
    s_setGuiStateFlag(*item.footer, Amethyst::GUI_STATE_HOVERED, hovered);
}

ContentBrowserPanel::ContentItemComponents &ContentBrowserPanel::acquirePoolItem(size_t index)
{
    if (index >= m_contentItemPool.size()) {
        // The InvisibleButton must be the topmost hittable node in the card, so the visual
        // children are marked non-interactable; otherwise they swallow the click/hover.
        ContentItemComponents item;

        item.container = m_contentContainer->add<Amethyst::Frame>();
        item.container->setClasses({"content-browser-card"});
        item.container->setBaseProperties({.clipsDescendants = true});

        item.icon = item.container->add<Amethyst::ImageLabel>();
        item.icon->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
            .interactable = false,
            .position = Amethyst::UDim2(0.5f, 0.0f, 0.5f, -CARD_FOOTER_HEIGHT * 0.5f),
            .size = Amethyst::UDim2::fromOffset(CARD_ICON_SIZE, CARD_ICON_SIZE),
        });

        item.footer = item.container->add<Amethyst::Frame>();
        item.footer->addClass("content-browser-card-footer");
        item.footer->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
            .interactable = false,
            .position = Amethyst::UDim2::fromScale(0.0f, 1.0f),
            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, CARD_FOOTER_HEIGHT),
        });

        item.name = item.footer->add<Amethyst::TextLabel>();
        item.name->addClass("content-browser-card-name");
        item.name->setBaseProperties({.interactable = false});

        item.type = item.footer->add<Amethyst::TextLabel>();
        item.type->addClass("content-browser-card-kind");
        item.type->setBaseProperties({.interactable = false});

        item.typeBar = item.footer->add<Amethyst::Frame>();
        item.typeBar->setBaseProperties({
            .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
            .interactable = false,
            .position = Amethyst::UDim2::fromScale(0.0f, 1.0f),
            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, CARD_TYPEBAR_HEIGHT),
        });

        item.action = item.container->add<Amethyst::InvisibleButton>();
        item.action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
        item.tooltip = item.action->addExtension<Amethyst::UITooltip>();

        item.attached = true;
        m_contentItemPool.push_back(std::move(item));

        const size_t poolIndex = m_contentItemPool.size() - 1;
        m_contentItemPool[poolIndex].action->track(m_contentItemPool[poolIndex].action->onHoverChanged.connect(
            [this, poolIndex](bool hovered) { applyItemHover(m_contentItemPool[poolIndex], hovered); }));
    }

    auto &item = m_contentItemPool[index];
    if (!item.attached) {
        item.container->setBaseProperties({.visible = true});
        item.attached = true;
    }

    // a pooled card can come back from having been named or selected, so it starts every refresh showing neither
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
