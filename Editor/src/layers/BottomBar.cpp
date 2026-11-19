#include "layers/BottomBar.h"

#include "app/EditorConfig.h"
#include "commands/EditorCommands.h"
#include "layers/CommandLine.h"
#include "panels/ContentBrowserPanel.h"

#include <rapture/app/Application.h>
#include <rapture/core/utils/Telemetry.h>
#include <rapture/core/utils/rp_assert.h>

#include <components/common.h>
#include <components/popup.h>
#include <components/text_label.h>
#include <components/ui_scope.h>
#include <modules/color.h>
#include <modules/text_processor.h>
#include <parsers/freetype/font_registry.h>

#include <format>

static constexpr float BOTTOM_BAR_PADDING = 4.0f;
static constexpr float CONTENT_BROWSER_BUTTON_WIDTH = 150.0f;
static constexpr float SEARCHBAR_CONTAINER_WIDTH = 400.0f;
static constexpr float ISLAND_SEPARATOR_WIDTH = 2.0f;
static constexpr float STATS_ISLAND_PADDING = 12.0f;
static constexpr float STAT_GAP = 14.0f;
static constexpr float STAT_UNIT_GAP = 4.0f;
static constexpr uint64_t BYTES_PER_MEGABYTE = 1024ull * 1024ull;

static constexpr std::array<std::string_view, BottomBar::BOTTOM_BAR_STAT_COUNT> STAT_WIDEST_VALUES = {"999.9", "9999", "99999"};
static constexpr std::array<std::string_view, BottomBar::BOTTOM_BAR_STAT_COUNT> STAT_UNITS = {"ms", "fps", "MB VRAM"};

static float s_measureLabelTextWidth(const Amethyst::TextProcessor &textProcessor, std::string_view text,
                                     const Amethyst::TextStyleProperties &style)
{
    const Amethyst::FontId font = Amethyst::FontRegistry::instance().findFont(style.fontFamily);
    return textProcessor.measureTextAtlas(std::string(text), font, static_cast<uint32_t>(style.fontSize)).x;
}

static bool s_workspaceContainsContentBrowserPanel(Workspace *workspace)
{
    if (workspace == nullptr) {
        return false;
    }

    for (const auto &panel : workspace->getPanels()) {
        if (dynamic_cast<ContentBrowserPanel *>(panel.get()) != nullptr) {
            return true;
        }
    }
    return false;
}

BottomBar::BottomBar(Amethyst::Window *window, const EditorContext &context) : m_editor(context), m_window(window)
{
    const float height = m_editor.config->getFloat(EDITOR_CONFIG_BOTTOM_BAR_HEIGHT);

    m_root = window->add<Amethyst::Frame>();
    m_root->name = "Bottom Bar";
    m_root->addClass("bottom-bar");
    m_root->setBaseProperties({
        .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -height),
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, height),
    });

    auto rootScope = Amethyst::UIScope(*m_root);
    rootScope.textButton(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, CONTENT_BROWSER_BUTTON_WIDTH, 1.0f, 0.0f),
                },
            .label = "Content Browser",
        },
        [this](Amethyst::TextButtonScope &b) {
            m_contentBrowserBtn = &b.component;
            m_contentBrowserBtn->addClass("bottom-bar-button");
            m_contentBrowserBtn->track(m_contentBrowserBtn->onMouseButton1ClickCb.connect([this]() { toggleContentBrowser(); }));
        });

    rootScope.frame(
        {
            .base =
                {
                    .padding = Amethyst::UDim4::fromOffset(BOTTOM_BAR_PADDING),
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH + ISLAND_SEPARATOR_WIDTH, 0.0f),
                    .size = Amethyst::UDim2(0.0f, SEARCHBAR_CONTAINER_WIDTH, 1.0f, 0.0f),
                },
        },
        [this](Amethyst::FrameScope &cont) {
            cont.component.addClass("bottom-bar");
            cont.textInput({.base = {.size = Amethyst::UDim2::fromScale(1.0f)}, .placeholder = "COMMAND"},
                           [this](Amethyst::TextInputScope &tis) {
                               tis.component.addClass("command-field");
                               m_commandInput = &tis.component;
                           });
        });
    addIslandSeparator(Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH, 0.0f));
    addIslandSeparator(Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH + SEARCHBAR_CONTAINER_WIDTH + ISLAND_SEPARATOR_WIDTH, 0.0f));
    setupStatsIsland();

    if (m_editor.commands != nullptr) {
        m_commandLine = std::make_unique<CommandLine>(*m_commandInput, *m_root, m_editor.commands->registry());
    }
    setupContentBrowserToggle();
}

BottomBar::~BottomBar() = default;

void BottomBar::addIslandSeparator(const Amethyst::UDim2 &position)
{
    Amethyst::UIScope(*m_root).frame({
        .classes = {"bottom-bar-separator"},
        .base =
            {
                .position = position,
                .size = Amethyst::UDim2(0.0f, ISLAND_SEPARATOR_WIDTH, 1.0f, 0.0f),
            },
    });
}

void BottomBar::setupStatsIsland()
{
    RP_ASSERT(m_editor.amethyst != nullptr, "the bottom bar was built without the ui context");
    const Amethyst::TextProcessor &textProcessor = m_editor.amethyst->getTextProcessor();

    Amethyst::Frame *island = nullptr;
    Amethyst::UIScope(*m_root).frame(
        {
            .classes = {"bottom-bar"},
            .base =
                {
                    .anchorPoint = Amethyst::vec2(1.0f, 0.0f),
                    .position = Amethyst::UDim2::fromScale(1.0f, 0.0f),
                },
        },
        [&island](Amethyst::FrameScope &f) { island = &f.component; });

    float cursor = STATS_ISLAND_PADDING;
    for (size_t stat = 0; stat < BOTTOM_BAR_STAT_COUNT; ++stat) {
        Amethyst::UIScope islandScope(*island);
        islandScope.textLabel({.classes = {"bottom-bar-stat-value"}},
                              [this, stat](Amethyst::TextLabelScope &tl) { m_statValueLabels[stat] = &tl.component; });
        Amethyst::TextLabel *value = m_statValueLabels[stat];
        const float valueWidth = s_measureLabelTextWidth(textProcessor, STAT_WIDEST_VALUES[stat], value->getTextStyleProperties());
        value->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(cursor, 0.0f),
            .size = Amethyst::UDim2(0.0f, valueWidth, 1.0f, 0.0f),
        });
        cursor += valueWidth + STAT_UNIT_GAP;

        Amethyst::TextLabel *unit = nullptr;
        islandScope.textLabel({.classes = {"bottom-bar-stat-unit"}, .label = std::string(STAT_UNITS[stat])},
                              [&unit](Amethyst::TextLabelScope &tl) { unit = &tl.component; });
        const float unitWidth = s_measureLabelTextWidth(textProcessor, STAT_UNITS[stat], unit->getTextStyleProperties());
        unit->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(cursor, 0.0f),
            .size = Amethyst::UDim2(0.0f, unitWidth, 1.0f, 0.0f),
        });
        cursor += unitWidth + STAT_GAP;
    }

    const float islandWidth = cursor - STAT_GAP + STATS_ISLAND_PADDING;
    island->setBaseProperties({.size = Amethyst::UDim2(0.0f, islandWidth, 1.0f, 0.0f)});
    addIslandSeparator(Amethyst::UDim2(1.0f, -islandWidth - ISLAND_SEPARATOR_WIDTH, 0.0f, 0.0f));
}

void BottomBar::onUpdate()
{
    const Rapture::Telemetry &telemetry = Rapture::Application::getInstance().getTelemetry();
    if (telemetry.frameTimeMs == m_shownFrameTimeMs && telemetry.vramUsedBytes == m_shownVramUsedBytes) {
        return;
    }
    m_shownFrameTimeMs = telemetry.frameTimeMs;
    m_shownVramUsedBytes = telemetry.vramUsedBytes;

    const float framesPerSecond = telemetry.frameTimeMs > 0.0f ? 1000.0f / telemetry.frameTimeMs : 0.0f;
    m_statValueLabels[BOTTOM_BAR_STAT_FRAME_TIME]->setText(std::format("{:.1f}", telemetry.frameTimeMs));
    m_statValueLabels[BOTTOM_BAR_STAT_FPS]->setText(std::format("{:.0f}", framesPerSecond));
    m_statValueLabels[BOTTOM_BAR_STAT_VRAM]->setText(std::format("{}", telemetry.vramUsedBytes / BYTES_PER_MEGABYTE));
}

void BottomBar::setupContentBrowserToggle()
{
    Amethyst::UIScope(*m_root).popup(
        {
            .base = {.size = Amethyst::UDim2::fromScale(0.7f, 0.45f)},
            .style = {.backgroundColor = Amethyst::Color3(0.13f), .borderPixelSize = 0.0f},
            .placement = Amethyst::PopupPlacement::ABOVE,
        },
        [this](Amethyst::PopupScope &p) { m_contentBrowserPopup = &p.component; });

    WorkspaceContext context = m_currWorkspace != nullptr ? m_currWorkspace->getContext() : WorkspaceContext{.editor = &m_editor};
    m_contentBrowserPanel = std::make_unique<ContentBrowserPanel>(context, [this]() { dockContentBrowser(); });
    m_contentBrowserPanel->attachTo(*m_contentBrowserPopup);
}

void BottomBar::dockContentBrowser()
{
    if (m_currWorkspace == nullptr) {
        return;
    }

    m_currWorkspace->addPanel(std::move(m_contentBrowserPanel), Amethyst::DockZone::BOTTOM);
    m_contentBrowserPanel = nullptr;
    m_contentBrowserPopup = nullptr;
}

void BottomBar::setCurrentWorkspace(Workspace *workspace)
{
    m_currWorkspace = workspace;
    if (m_contentBrowserPanel != nullptr && workspace != nullptr) {
        m_contentBrowserPanel->setContext(workspace->getContext());
    }
}

void BottomBar::toggleContentBrowser()
{
    if (m_contentBrowserPopup == nullptr || m_contentBrowserPanel == nullptr) {
        if (s_workspaceContainsContentBrowserPanel(m_currWorkspace)) {
            return;
        }
        setupContentBrowserToggle();
    }

    if (m_contentBrowserPopup->isOpen()) {
        m_contentBrowserPopup->close();
    } else {
        m_contentBrowserPopup->open(m_contentBrowserBtn);
    }
}
