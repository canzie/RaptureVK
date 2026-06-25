#include "FileBrowser.h"
#include "Icons.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

#define COL_TOOLBAR  Amethyst::Color3::fromHex(0x202020)
#define COL_PANEL    Amethyst::Color3::fromHex(0x282828)
#define COL_PANEL_2  Amethyst::Color3::fromHex(0x2e2e2e)
#define COL_LIST_BG   Amethyst::Color3::fromHex(0x2b2b2b)
#define COL_LIST_BG_4 Amethyst::Color4(0.169f, 0.169f, 0.169f, 1.0f)
#define COL_ROW_ALT_4 Amethyst::Color4(0.188f, 0.188f, 0.188f, 1.0f)
#define COL_ACCENT   Amethyst::Color3::fromHex(0x4772b3)
#define COL_HOVER    Amethyst::Color3::fromHex(0x363636)
#define COL_APP       Amethyst::Color3::fromHex(0x0d0d0d)
#define COL_LINE      Amethyst::Color3::fromHex(0x252525)
#define COL_APP_HOVER Amethyst::Color3::fromHex(0x1a1a1a)

#define COL_TEXT          Amethyst::Color4(0.92f, 0.92f, 0.92f, 1.0f)
#define COL_TEXT_STRONG   Amethyst::Color4(0.95f, 0.95f, 0.95f, 1.0f)
#define COL_TEXT_MUTED    Amethyst::Color4(1.0f, 1.0f, 1.0f, 0.62f)
#define COL_TEXT_DIM      Amethyst::Color4(1.0f, 1.0f, 1.0f, 0.42f)
#define COL_TEXT_TERTIARY Amethyst::Color4(1.0f, 1.0f, 1.0f, 0.28f)
#define COL_ICON          Amethyst::Color4(0.8f, 0.8f, 0.8f, 1.0f)
#define COL_FOLDER        Amethyst::Color4(0.85f, 0.72f, 0.4f, 1.0f)

static constexpr float TOP_BAR_HEIGHT = 44.0f;
static constexpr float SIDE_BAR_WIDTH = 220.0f;
static constexpr float LIST_HEADER_HEIGHT = 28.0f;
static constexpr float STATUS_BAR_HEIGHT = 24.0f;
static constexpr float FOOTER_HEIGHT = 52.0f;
static constexpr float ROW_HEIGHT = 30.0f;
static constexpr float SECTION_HEADER_HEIGHT = 26.0f;
static constexpr float BOOKMARK_HEIGHT = 26.0f;

static constexpr float CONTENT_PADDING = 10.0f;

static std::string s_formatTime(const std::filesystem::path &path)
{
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return "—";
    }
    // Map the filesystem clock onto the system clock without C++20 clock_cast (wider toolchain support).
    auto sysTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sysTime);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

static std::string s_formatSize(uintmax_t bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    char buf[32];
    if (unit == 0) {
        std::snprintf(buf, sizeof(buf), "%llu %s", static_cast<unsigned long long>(bytes), units[unit]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    }
    return buf;
}

// A nav icon box: a 15x15 centered icon over a full-size hit surface, with the
// surface fill and icon tint swapping on hover.
static void s_navButton(Amethyst::FrameScope &box, const char *svg, const Amethyst::Color3 &restColor, float restAlpha)
{
    Amethyst::Frame *surface = &box.component;

    auto *icon = surface->add<Amethyst::ImageLabel>();
    icon->setBaseProperties({
        .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
        .interactable = false,
        .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
        .size = Amethyst::UDim2::fromOffset(15.0f, 15.0f),
    });
    icon->setBaseStyleProperties({.backgroundTransparency = 1.0f});
    icon->setImageStyleProperties({.imageColor = COL_TEXT_MUTED});
    icon->setSvg(svg);

    auto *action = surface->add<Amethyst::InvisibleButton>();
    action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
    action->track(action->onHoverChanged.connect([surface, icon, restColor, restAlpha](bool hovered) {
        surface->setBaseStyleProperties(
            {.backgroundColor = hovered ? COL_APP_HOVER : restColor, .backgroundTransparency = hovered ? 0.0f : restAlpha});
        icon->setImageStyleProperties({.imageColor = hovered ? COL_TEXT : COL_TEXT_MUTED});
    }));
}

FileBrowser::FileBrowser(Amethyst::Instance &parent)
{
    m_root = parent.add<Amethyst::Frame>();
    buildContent();
}

FileBrowser::~FileBrowser()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        m_root->parent->removeChild(m_root);
    }
}

void FileBrowser::buildContent()
{
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });

    m_root->name = "File Browser";
    m_root->setBaseProperties({.position = Amethyst::UDim2::fromScale(0.0f), .size = Amethyst::UDim2::fromScale(1.0f)});
    m_root->setBaseStyleProperties({.backgroundColor = COL_PANEL});

    m_currentDirectory = std::filesystem::current_path();

    setupTopBar();
    setupSideBar();
    setupListArea();
    setupStatusBar();
    setupFooter();

    populate();
}

void FileBrowser::setupTopBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromScale(0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TOP_BAR_HEIGHT),
                },
            .style = {.backgroundColor = COL_TOOLBAR},
        },
        [this](Amethyst::FrameScope &top) {
            static constexpr float NAV_BTN_W = 28.0f;
            static constexpr float NAV_BTN_H = 26.0f;
            static constexpr float NAV_GROUP_PAD = 2.0f;
            static constexpr float NAV_GROUP_GAP = 1.0f;
            static constexpr float NEW_FOLDER_SIZE = 30.0f;
            const float groupWidth = NAV_GROUP_PAD * 2.0f + 4.0f * NAV_BTN_W + 3.0f * NAV_GROUP_GAP;

            const char *navIcons[] = {Icons::SVG_NAV_BACK, Icons::SVG_NAV_FORWARD, Icons::SVG_NAV_UP, Icons::SVG_REFRESH};

            top.frame(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .padding = {Amethyst::UDim::fromOffset(NAV_GROUP_PAD), Amethyst::UDim::fromOffset(NAV_GROUP_PAD),
                                        Amethyst::UDim::fromOffset(NAV_GROUP_PAD), Amethyst::UDim::fromOffset(NAV_GROUP_PAD)},
                            .position = Amethyst::UDim2(0.0f, CONTENT_PADDING, 0.5f, 0.0f),
                            .size = Amethyst::UDim2::fromOffset(groupWidth, NAV_BTN_H + NAV_GROUP_PAD * 2.0f),
                        },
                    .style = {.backgroundColor = COL_APP, .borderPixelSize = 1.0f, .borderColor = COL_LINE, .cornerRadius = 3.0f},
                },
                [&navIcons](Amethyst::FrameScope &group) {
                    auto *layout = group.component.addExtension<Amethyst::UIListLayout>();
                    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                    layout->innerPadding = Amethyst::UDim::fromOffset(NAV_GROUP_GAP);

                    for (int i = 0; i < 4; i++) {
                        group.frame(
                            {
                                .base = {.layoutOrder = static_cast<uint32_t>(i),
                                         .size = Amethyst::UDim2::fromOffset(NAV_BTN_W, NAV_BTN_H)},
                                .style = {.backgroundColor = COL_APP, .backgroundTransparency = 1.0f, .cornerRadius = 2.0f},
                            },
                            [icon = navIcons[i]](Amethyst::FrameScope &slot) { s_navButton(slot, icon, COL_APP, 1.0f); });
                    }
                });

            // standalone new-folder button with a border to match the nav group
            top.frame(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .position = Amethyst::UDim2(0.0f, CONTENT_PADDING + groupWidth + 8.0f, 0.5f, 0.0f),
                            .size = Amethyst::UDim2::fromOffset(NEW_FOLDER_SIZE, NEW_FOLDER_SIZE),
                        },
                    .style = {.backgroundColor = COL_APP, .borderPixelSize = 1.0f, .borderColor = COL_LINE, .cornerRadius = 3.0f},
                },
                [](Amethyst::FrameScope &slot) { s_navButton(slot, Icons::SVG_FOLDER_PLUS, COL_APP, 0.0f); });

            const float pathStart = CONTENT_PADDING + groupWidth + 8.0f + NEW_FOLDER_SIZE + 8.0f;
            const float searchWidth = 200.0f;

            // editable path string (not breadcrumbs) — fills the gap up to the search box.
            top.frame(
                {
                    .classes = {"generic-input-field"},
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .position = Amethyst::UDim2(0.0f, pathStart, 0.5f, 0.0f),
                            .size = Amethyst::UDim2(1.0f, -(pathStart + searchWidth + CONTENT_PADDING + 8.0f), 0.0f, 28.0f),
                        },
                    .style = {.cornerRadius = 2.0f},
                },
                [this](Amethyst::FrameScope &field) {
                    field.textInput(
                        {
                            .base =
                                {
                                    .position = Amethyst::UDim2(0.0f, 8.0f, 0.0f, 0.0f),
                                    .size = Amethyst::UDim2(1.0f, -16.0f, 1.0f, 0.0f),
                                },
                            .style = {.backgroundTransparency = 1.0f},
                            .textInput = {.text = {.textColor = COL_TEXT, .textYAlignment = Amethyst::TextYAlignment::CENTER}},
                            .placeholder = "Path",
                        },
                        [this](Amethyst::TextInputScope &ti) {
                            m_pathInput = &ti.component;
                            m_pathInput->setText(m_currentDirectory.string());
                        });
                });

            // right-aligned search / filter box
            top.frame(
                {
                    .classes = {"generic-input-field"},
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                            .position = Amethyst::UDim2(1.0f, -CONTENT_PADDING, 0.5f, 0.0f),
                            .size = Amethyst::UDim2(0.0f, searchWidth, 0.0f, 28.0f),
                        },
                    .style = {.cornerRadius = 2.0f},
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
                        .image = {.imageColor = COL_TEXT_DIM},
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
                            .textInput = {.text = {.textColor = COL_TEXT, .textYAlignment = Amethyst::TextYAlignment::CENTER}},
                            .placeholder = "Search",
                        },
                        [this](Amethyst::TextInputScope &ti) { m_searchInput = &ti.component; });
                });
        });
}

static Amethyst::CollapsibleHeaderStyleProperties s_sectionHeaderStyle()
{
    return {
        .titleStyle =
            {
                .fontSize = 11.0f,
                .textColor = COL_TEXT_MUTED,
                .textXAlignment = Amethyst::TextXAlignment::LEFT,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
            },
        .headerHeight = SECTION_HEADER_HEIGHT,
        .headerTransparency = 1.0f,
        .indicatorSize = 12.0f,
        .indicatorColor = COL_TEXT_DIM,
    };
}

// One sidebar bookmark row: icon + label, with hover tint.
static void s_addBookmark(Amethyst::UIScope &scope, uint32_t order, const char *icon, const std::string &label, bool active)
{
    scope.frame(
        {
            .base =
                {
                    .layoutOrder = order,
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, BOOKMARK_HEIGHT),
                },
            .style = {.backgroundColor = COL_ACCENT, .backgroundTransparency = active ? 0.78f : 1.0f, .cornerRadius = 3.0f},
        },
        [icon, &label, active](Amethyst::FrameScope &bm) {
            auto *action = bm.component.add<Amethyst::InvisibleButton>();
            action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
            Amethyst::Frame *row = &bm.component;
            action->track(action->onHoverChanged.connect([row, active](bool hovered) {
                row->setBaseStyleProperties({.backgroundColor = active ? COL_ACCENT : COL_HOVER,
                                             .backgroundTransparency = (active || hovered) ? (active ? 0.78f : 0.0f) : 1.0f});
            }));

            auto *iconLabel = action->add<Amethyst::ImageLabel>();
            iconLabel->setBaseProperties({
                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                .interactable = false,
                .position = Amethyst::UDim2(0.0f, 8.0f, 0.5f, 0.0f),
                .size = Amethyst::UDim2::fromOffset(14.0f, 14.0f),
            });
            iconLabel->setBaseStyleProperties({.backgroundTransparency = 1.0f});
            iconLabel->setImageStyleProperties({.imageColor = active ? COL_FOLDER : COL_ICON});

            auto *text = action->add<Amethyst::TextLabel>();
            text->setBaseProperties({
                .interactable = false,
                .position = Amethyst::UDim2(0.0f, 30.0f, 0.0f, 0.0f),
                .size = Amethyst::UDim2(1.0f, -38.0f, 1.0f, 0.0f),
            });
            text->setBaseStyleProperties({.backgroundTransparency = 1.0f});
            text->setTextStyleProperties({
                .fontSize = 12.0f,
                .textColor = COL_TEXT,
                .textXAlignment = Amethyst::TextXAlignment::LEFT,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
                .textTruncate = Amethyst::TextTruncate::AT_END,
            });
            text->setText(label);
        });
}

struct BookmarkDef {
    const char *icon;
    std::string label;
    bool active;
};

// A collapsible sidebar section holding a vertical list of bookmark rows. The
// header does not auto-size to its content, so the expanded height is computed
// up front from the item count.
static void s_addSection(Amethyst::UIScope &side, uint32_t order, const std::string &title,
                         const std::vector<BookmarkDef> &items)
{
    static constexpr float ITEM_GAP = 1.0f;
    static constexpr float PAD_TOP = 4.0f;
    static constexpr float PAD_BOTTOM = 6.0f;

    const float itemsHeight = items.empty()
                                  ? 0.0f
                                  : static_cast<float>(items.size()) * BOOKMARK_HEIGHT +
                                        static_cast<float>(items.size() - 1) * ITEM_GAP;
    const float contentHeight = PAD_TOP + itemsHeight + PAD_BOTTOM;

    side.collapsibleHeader(
        {
            .base =
                {
                    .clipsDescendants = true,
                    .layoutOrder = order,
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, SECTION_HEADER_HEIGHT + contentHeight),
                },
            .header = s_sectionHeaderStyle(),
            .title = title,
        },
        [&items, contentHeight](Amethyst::CollapsibleHeaderScope &ch) {
            ch.frame(
                {
                    .base =
                        {
                            .padding = {Amethyst::UDim::fromOffset(PAD_TOP), Amethyst::UDim::fromOffset(6.0f),
                                        Amethyst::UDim::fromOffset(PAD_BOTTOM), Amethyst::UDim::fromOffset(6.0f)},
                            .position = Amethyst::UDim2::fromScale(0.0f),
                            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, contentHeight),
                        },
                    .style = {.backgroundTransparency = 1.0f},
                },
                [&items](Amethyst::FrameScope &itemsFrame) {
                    auto *layout = itemsFrame.component.addExtension<Amethyst::UIListLayout>();
                    layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
                    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_TOP;
                    layout->innerPadding = Amethyst::UDim::fromOffset(ITEM_GAP);

                    Amethyst::UIScope itemScope(itemsFrame.component);
                    uint32_t itemOrder = 0;
                    for (const auto &bm : items) {
                        s_addBookmark(itemScope, itemOrder++, bm.icon, bm.label, bm.active);
                    }
                });
        });
}

void FileBrowser::setupSideBar()
{
    Amethyst::UIScope(*m_root).scrollingFrame(
        {
            .base =
                {
                    .clipsDescendants = true,
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, TOP_BAR_HEIGHT),
                    .size = Amethyst::UDim2(0.0f, SIDE_BAR_WIDTH, 1.0f, -(TOP_BAR_HEIGHT + STATUS_BAR_HEIGHT + FOOTER_HEIGHT)),
                },
            .style = {.backgroundColor = COL_PANEL},
            .scroll =
                {
                    .scrollAxis = Amethyst::ScrollAxis::Y,
                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                    .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                },
        },
        [this](Amethyst::ScrollingFrameScope &sf) {
            auto *layout = sf.component.addExtension<Amethyst::UIListLayout>();
            layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
            layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
            layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_TOP;

            Amethyst::UIScope side(sf.component);

            s_addSection(side, 0, "FAVORITES", {{Icons::SVG_PIN, "Project Root", false}});
            s_addSection(side, 1, "SYSTEM",
                         {
                             {Icons::SVG_FOLDER, "Home", false},
                             {Icons::SVG_FOLDER, "Desktop", false},
                             {Icons::SVG_FOLDER, "Documents", false},
                             {Icons::SVG_FOLDER, "Downloads", false},
                         });
            s_addSection(side, 2, "PROJECT", {{Icons::SVG_FOLDER, m_currentDirectory.filename().string(), true}});
            s_addSection(side, 3, "RECENT", {{Icons::SVG_SCRIPT, "scene.glb", false}});
        });
}

void FileBrowser::setupListArea()
{
    const float listTop = TOP_BAR_HEIGHT;
    const float bottomReserve = STATUS_BAR_HEIGHT + FOOTER_HEIGHT;

    Amethyst::UIScope(*m_root).table(
        {
            .base =
                {
                    .clipsDescendants = true,
                    .position = Amethyst::UDim2(0.0f, SIDE_BAR_WIDTH, 0.0f, listTop),
                    .size = Amethyst::UDim2(1.0f, -SIDE_BAR_WIDTH, 1.0f, -(listTop + bottomReserve)),
                },
            .style = {.backgroundColor = COL_LIST_BG},
            .table =
                {
                    .rowHeight = ROW_HEIGHT,
                    .cellPadding = {Amethyst::UDim::fromOffset(0.0f), Amethyst::UDim::fromOffset(CONTENT_PADDING),
                                    Amethyst::UDim::fromOffset(0.0f), Amethyst::UDim::fromOffset(CONTENT_PADDING)},
                    .separatorMode = Amethyst::TableSeparatorMode::OFF,
                    .showHeader = true,
                    .headerHeight = LIST_HEADER_HEIGHT,
                    .headerColor = COL_PANEL_2,
                    .header =
                        {
                            .fontSize = 11.0f,
                            .textColor = COL_TEXT_DIM,
                            .textXAlignment = Amethyst::TextXAlignment::LEFT,
                            .textYAlignment = Amethyst::TextYAlignment::CENTER,
                        },
                    .rowBackgroundColor = COL_LIST_BG_4,
                    .rowAlternateColor = COL_ROW_ALT_4,
                },
        },
        [this](Amethyst::TableScope &t) {
            m_table = &t.component;
            // Weights are relative widths (no real pixel lock yet). Name dominates; the
            // metadata columns stay compact. The icon lives inside the name cell.
            t.column("NAME", 6.0f);
            t.column("SIZE", 1.0f);
            t.column("TYPE", 1.1f);
            t.column("DATE", 1.6f);
        });
}

void FileBrowser::setupStatusBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, -(STATUS_BAR_HEIGHT + FOOTER_HEIGHT)),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, STATUS_BAR_HEIGHT),
                },
            .style = {.backgroundColor = COL_PANEL_2},
        },
        [this](Amethyst::FrameScope &status) {
            status.textLabel(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2(0.0f, 12.0f, 0.0f, 0.0f),
                            .size = Amethyst::UDim2(0.5f, -12.0f, 1.0f, 0.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f},
                    .text =
                        {
                            .fontSize = 11.0f,
                            .textColor = COL_TEXT_TERTIARY,
                            .textXAlignment = Amethyst::TextXAlignment::LEFT,
                            .textYAlignment = Amethyst::TextYAlignment::CENTER,
                        },
                    .label = "0 items",
                },
                [this](Amethyst::TextLabelScope &lbl) { m_statusLabel = &lbl.component; });

            status.textLabel(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.0f),
                            .position = Amethyst::UDim2(1.0f, -12.0f, 0.0f, 0.0f),
                            .size = Amethyst::UDim2(0.5f, -12.0f, 1.0f, 0.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f},
                    .text =
                        {
                            .fontSize = 11.0f,
                            .textColor = COL_TEXT_TERTIARY,
                            .textXAlignment = Amethyst::TextXAlignment::RIGHT,
                            .textYAlignment = Amethyst::TextYAlignment::CENTER,
                        },
                    .label = "",
                },
                [this](Amethyst::TextLabelScope &lbl) { m_selectionLabel = &lbl.component; });
        });
}

void FileBrowser::setupFooter()
{
    const float openWidth = 84.0f;
    const float cancelWidth = 84.0f;
    const float filterWidth = 110.0f;
    const float gap = 8.0f;
    const float rightReserve = CONTENT_PADDING + openWidth + gap + cancelWidth + gap + filterWidth + gap;
    const float fieldLeft = 58.0f;

    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
                    .position = Amethyst::UDim2(0.0f, 0.0f, 1.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, FOOTER_HEIGHT),
                },
            .style = {.backgroundColor = COL_TOOLBAR},
        },
        [&](Amethyst::FrameScope &foot) {
            foot.textLabel({
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                        .position = Amethyst::UDim2(0.0f, 12.0f, 0.5f, 0.0f),
                        .size = Amethyst::UDim2::fromOffset(40.0f, 20.0f),
                    },
                .style = {.backgroundTransparency = 1.0f},
                .text =
                    {
                        .fontSize = 12.0f,
                        .textColor = COL_TEXT_MUTED,
                        .textXAlignment = Amethyst::TextXAlignment::LEFT,
                        .textYAlignment = Amethyst::TextYAlignment::CENTER,
                    },
                .label = "File",
            });

            // filename field (flex)
            foot.frame(
                {
                    .classes = {"generic-input-field"},
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                            .position = Amethyst::UDim2(0.0f, fieldLeft, 0.5f, 0.0f),
                            .size = Amethyst::UDim2(1.0f, -(fieldLeft + rightReserve), 0.0f, 30.0f),
                        },
                    .style = {.cornerRadius = 2.0f},
                },
                [this](Amethyst::FrameScope &field) {
                    field.textInput(
                        {
                            .base =
                                {
                                    .position = Amethyst::UDim2(0.0f, 8.0f, 0.0f, 0.0f),
                                    .size = Amethyst::UDim2(1.0f, -16.0f, 1.0f, 0.0f),
                                },
                            .style = {.backgroundTransparency = 1.0f},
                            .textInput = {.text = {.textColor = COL_TEXT, .textYAlignment = Amethyst::TextYAlignment::CENTER}},
                            .placeholder = "No file selected",
                        },
                        [this](Amethyst::TextInputScope &ti) { m_filenameInput = &ti.component; });
                });

            // filter dropdown — styled as a button for now ("All Assets" + caret)
            foot.frame(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                            .position = Amethyst::UDim2(1.0f, -(CONTENT_PADDING + openWidth + gap + cancelWidth + gap), 0.5f, 0.0f),
                            .size = Amethyst::UDim2(0.0f, filterWidth, 0.0f, 30.0f),
                        },
                    .style = {.backgroundColor = COL_PANEL_2, .cornerRadius = 3.0f},
                },
                [](Amethyst::FrameScope &filter) {
                    filter.textLabel({
                        .base =
                            {
                                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                .position = Amethyst::UDim2(0.0f, 10.0f, 0.5f, 0.0f),
                                .size = Amethyst::UDim2(1.0f, -28.0f, 1.0f, 0.0f),
                            },
                        .style = {.backgroundTransparency = 1.0f},
                        .text =
                            {
                                .fontSize = 12.0f,
                                .textColor = COL_TEXT,
                                .textXAlignment = Amethyst::TextXAlignment::LEFT,
                                .textYAlignment = Amethyst::TextYAlignment::CENTER,
                            },
                        .label = "All Assets",
                    });
                    filter.imageLabel({
                        .base =
                            {
                                .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                                .position = Amethyst::UDim2(1.0f, -8.0f, 0.5f, 0.0f),
                                .size = Amethyst::UDim2::fromOffset(12.0f, 12.0f),
                            },
                        .style = {.backgroundTransparency = 1.0f},
                        .image = {.imageColor = COL_TEXT_DIM},
                        .svg = Icons::SVG_CARET_DOWN,
                    });
                });

            const Amethyst::TextStyleProperties btnText{
                .fontSize = 12.0f,
                .textColor = COL_TEXT,
                .textXAlignment = Amethyst::TextXAlignment::CENTER,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
            };

            foot.textButton({
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                        .position = Amethyst::UDim2(1.0f, -(CONTENT_PADDING + openWidth + gap), 0.5f, 0.0f),
                        .size = Amethyst::UDim2(0.0f, cancelWidth, 0.0f, 30.0f),
                    },
                .style = {.backgroundColor = COL_PANEL_2, .cornerRadius = 3.0f},
                .text = btnText,
                .label = "Cancel",
            });

            foot.textButton({
                .classes = {"primary"},
                .base =
                    {
                        .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                        .position = Amethyst::UDim2(1.0f, -CONTENT_PADDING, 0.5f, 0.0f),
                        .size = Amethyst::UDim2(0.0f, openWidth, 0.0f, 30.0f),
                    },
                .style = {.cornerRadius = 3.0f},
                .text = btnText,
                .label = "Open",
            });
        });
}

// One muted, single-aligned text cell filling its table cell.
static void s_textCell(Amethyst::UIScope &s, const std::string &text, const Amethyst::Color4 &color,
                       Amethyst::TextXAlignment align)
{
    s.textLabel({
        .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        .style = {.backgroundTransparency = 1.0f},
        .text =
            {
                .fontSize = 11.0f,
                .textColor = color,
                .textXAlignment = align,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
                .textTruncate = Amethyst::TextTruncate::AT_END,
            },
        .label = text,
    });
}

void FileBrowser::populate()
{
    if (m_table == nullptr || !std::filesystem::exists(m_currentDirectory)) {
        return;
    }

    std::vector<std::filesystem::directory_entry> entries;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(m_currentDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        entries.push_back(*it);
    }

    std::sort(entries.begin(), entries.end(),
              [](const std::filesystem::directory_entry &a, const std::filesystem::directory_entry &b) {
                  bool aDir = a.is_directory();
                  bool bDir = b.is_directory();
                  if (aDir != bDir) {
                      return aDir > bDir;
                  }
                  return a.path().filename().string() < b.path().filename().string();
              });

    Amethyst::TableScope ts(*m_table);
    for (const auto &entry : entries) {
        const bool isDir = entry.is_directory();
        const std::string filename = entry.path().filename().string();
        const std::string type = isDir ? "Folder" : (entry.path().has_extension() ? entry.path().extension().string() : "File");
        const std::string date = s_formatTime(entry.path());
        std::string sizeText = "—";
        if (!isDir) {
            std::error_code sizeEc;
            uintmax_t bytes = std::filesystem::file_size(entry.path(), sizeEc);
            if (!sizeEc) {
                sizeText = s_formatSize(bytes);
            }
        }

        ts.row([=](Amethyst::TableRowScope &r) {
            // Name cell carries the icon inline (no dedicated icon column).
            r.cell([=](Amethyst::UIScope &s) {
                 s.imageLabel({
                     .base =
                         {
                             .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                             .position = Amethyst::UDim2(0.0f, 0.0f, 0.5f, 0.0f),
                             .size = Amethyst::UDim2::fromOffset(15.0f, 15.0f),
                         },
                     .style = {.backgroundTransparency = 1.0f},
                     .image = {.imageColor = isDir ? COL_FOLDER : COL_ICON},
                     .svg = isDir ? Icons::SVG_FOLDER : Icons::SVG_SCRIPT,
                 });
                 s.textLabel({
                     .base =
                         {
                             .position = Amethyst::UDim2(0.0f, 23.0f, 0.0f, 0.0f),
                             .size = Amethyst::UDim2(1.0f, -23.0f, 1.0f, 0.0f),
                         },
                     .style = {.backgroundTransparency = 1.0f},
                     .text =
                         {
                             .fontSize = 12.0f,
                             .textColor = isDir ? COL_TEXT_STRONG : COL_TEXT,
                             .textXAlignment = Amethyst::TextXAlignment::LEFT,
                             .textYAlignment = Amethyst::TextYAlignment::CENTER,
                             .textTruncate = Amethyst::TextTruncate::AT_END,
                         },
                     .label = filename,
                 });
             })
                .cell([=](Amethyst::UIScope &s) { s_textCell(s, sizeText, COL_TEXT_DIM, Amethyst::TextXAlignment::RIGHT); })
                .cell([=](Amethyst::UIScope &s) { s_textCell(s, type, COL_TEXT_DIM, Amethyst::TextXAlignment::LEFT); })
                .cell([=](Amethyst::UIScope &s) { s_textCell(s, date, COL_TEXT_DIM, Amethyst::TextXAlignment::LEFT); });
        });
    }

    if (m_statusLabel != nullptr) {
        m_statusLabel->setText(std::to_string(m_table->rowCount()) + " items");
    }
}
