#include "FileBrowser.h"
#include "Icons.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <components/dropdown.h>
#include <components/extensions/ui_list_layout.h>

#define COL_TOOLBAR  Amethyst::Color3::fromHex(0x202020)
#define COL_PANEL    Amethyst::Color3::fromHex(0x282828)
#define COL_PANEL_2  Amethyst::Color3::fromHex(0x2e2e2e)
#define COL_LIST_BG   Amethyst::Color3::fromHex(0x2b2b2b)
#define COL_LIST_BG_4 Amethyst::Color4(0.169f, 0.169f, 0.169f, 1.0f)
#define COL_ROW_ALT_4 Amethyst::Color4(0.188f, 0.188f, 0.188f, 1.0f)
#define COL_SELECTED_4 Amethyst::Color4(0.278f, 0.447f, 0.702f, 0.45f)
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

static constexpr float NAV_BTN_W = 28.0f;
static constexpr float NAV_BTN_H = 26.0f;
static constexpr float NAV_GROUP_PAD = 2.0f;
static constexpr float NAV_GROUP_GAP = 1.0f;
static constexpr float NEW_FOLDER_SIZE = 30.0f;

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

static std::string s_toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

FileBrowser::FileBrowser(Amethyst::Instance &parent, Mode mode) : m_mode(mode)
{
    m_root = parent.add<Amethyst::Frame>();
    buildContent();
}

FileBrowser::~FileBrowser()
{
    m_tick.unregister();
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

    readDirectory();
    updateNavState();

    if (Amethyst::Window *window = m_root->getWindow()) {
        m_tick = window->registerTick([this](float) { processDeferred(); });
    }
}

void FileBrowser::buildNavButton(Amethyst::FrameScope &slot, const char *svg, std::function<void()> onClick, NavButton *store)
{
    Amethyst::Frame *surface = &slot.component;

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

    if (store != nullptr) {
        store->surface = surface;
        store->icon = icon;
        store->enabled = true;
    }

    auto *action = surface->add<Amethyst::InvisibleButton>();
    action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});

    action->track(action->onHoverChanged.connect([surface, icon, store](bool hovered) {
        if (store != nullptr && !store->enabled) {
            return;
        }
        surface->setBaseStyleProperties(
            {.backgroundColor = hovered ? COL_APP_HOVER : COL_APP, .backgroundTransparency = hovered ? 0.0f : 1.0f});
        icon->setImageStyleProperties({.imageColor = hovered ? COL_TEXT : COL_TEXT_MUTED});
    }));

    if (onClick) {
        action->onMouseButton1ClickCb = [onClick = std::move(onClick), store]() {
            if (store == nullptr || store->enabled) {
                onClick();
            }
            return Amethyst::EventResult::CONSUMED;
        };
    }
}

void FileBrowser::applyNavEnabled(NavButton &nav, bool enabled)
{
    nav.enabled = enabled;
    if (nav.icon != nullptr) {
        nav.icon->setImageStyleProperties({.imageColor = enabled ? COL_TEXT_MUTED : COL_TEXT_TERTIARY});
    }
    if (nav.surface != nullptr) {
        nav.surface->setBaseStyleProperties({.backgroundColor = COL_APP, .backgroundTransparency = 1.0f});
    }
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
            const float groupWidth = NAV_GROUP_PAD * 2.0f + 4.0f * NAV_BTN_W + 3.0f * NAV_GROUP_GAP;

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
                [this](Amethyst::FrameScope &group) {
                    auto *layout = group.component.addExtension<Amethyst::UIListLayout>();
                    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                    layout->innerPadding = Amethyst::UDim::fromOffset(NAV_GROUP_GAP);

                    struct NavDef {
                        const char *svg;
                        std::function<void()> onClick;
                        NavButton *store;
                    };
                    NavDef defs[] = {
                        {Icons::SVG_NAV_BACK, [this] { goBack(); }, &m_backButton},
                        {Icons::SVG_NAV_FORWARD, [this] { goForward(); }, &m_forwardButton},
                        {Icons::SVG_NAV_UP, [this] { goUp(); }, nullptr},
                        {Icons::SVG_REFRESH, [this] { refresh(); }, nullptr},
                    };

                    for (uint32_t i = 0; i < 4; i++) {
                        group.frame(
                            {
                                .base = {.layoutOrder = i, .size = Amethyst::UDim2::fromOffset(NAV_BTN_W, NAV_BTN_H)},
                                .style = {.backgroundColor = COL_APP, .backgroundTransparency = 1.0f, .cornerRadius = 2.0f},
                            },
                            [this, &defs, i](Amethyst::FrameScope &slot) {
                                buildNavButton(slot, defs[i].svg, std::move(defs[i].onClick), defs[i].store);
                            });
                    }
                });

            // standalone new-folder button with a border to match the nav group (no behaviour yet)
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
                [this](Amethyst::FrameScope &slot) { buildNavButton(slot, Icons::SVG_FOLDER_PLUS, {}, nullptr); });

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
                            m_pathInput->onEnterPressed = [this]() {
                                std::filesystem::path typed = m_pathInput->getText();
                                if (std::filesystem::is_directory(typed)) {
                                    navigateTo(typed);
                                } else {
                                    m_pathInput->setText(m_currentDirectory.string());
                                }
                            };
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
                        [this](Amethyst::TextInputScope &ti) {
                            m_searchInput = &ti.component;
                            m_searchInput->onTextChanged = [this](const std::string &text) {
                                m_searchText = s_toLower(text);
                                populate();
                            };
                        });
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

struct BookmarkDef {
    const char *icon;
    std::string label;
    std::filesystem::path target;
    bool active;
};

// One sidebar bookmark row: icon + label, with hover tint. Clicking navigates to its target.
static void s_addBookmark(Amethyst::UIScope &scope, uint32_t order, const BookmarkDef &def,
                          const std::function<void(const std::filesystem::path &)> &navigate)
{
    scope.frame(
        {
            .base =
                {
                    .layoutOrder = order,
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, BOOKMARK_HEIGHT),
                },
            .style = {.backgroundColor = COL_ACCENT, .backgroundTransparency = def.active ? 0.78f : 1.0f, .cornerRadius = 3.0f},
        },
        [&def, navigate](Amethyst::FrameScope &bm) {
            auto *action = bm.component.add<Amethyst::InvisibleButton>();
            action->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)});
            Amethyst::Frame *row = &bm.component;
            bool active = def.active;
            action->track(action->onHoverChanged.connect([row, active](bool hovered) {
                row->setBaseStyleProperties({.backgroundColor = active ? COL_ACCENT : COL_HOVER,
                                             .backgroundTransparency = (active || hovered) ? (active ? 0.78f : 0.0f) : 1.0f});
            }));

            if (!def.target.empty()) {
                std::filesystem::path target = def.target;
                action->onMouseButton1ClickCb = [navigate, target]() {
                    navigate(target);
                    return Amethyst::EventResult::CONSUMED;
                };
            }

            auto *iconLabel = action->add<Amethyst::ImageLabel>();
            iconLabel->setBaseProperties({
                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                .interactable = false,
                .position = Amethyst::UDim2(0.0f, 8.0f, 0.5f, 0.0f),
                .size = Amethyst::UDim2::fromOffset(14.0f, 14.0f),
            });
            iconLabel->setBaseStyleProperties({.backgroundTransparency = 1.0f});
            iconLabel->setImageStyleProperties({.imageColor = active ? COL_FOLDER : COL_ICON});
            iconLabel->setSvg(def.icon);

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
            text->setText(def.label);
        });
}

// A collapsible sidebar section holding a vertical list of bookmark rows. The
// header does not auto-size to its content, so the expanded height is computed
// up front from the item count.
static void s_addSection(Amethyst::UIScope &side, uint32_t order, const std::string &title,
                         const std::vector<BookmarkDef> &items,
                         const std::function<void(const std::filesystem::path &)> &navigate)
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
        [&items, contentHeight, navigate](Amethyst::CollapsibleHeaderScope &ch) {
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
                [&items, navigate](Amethyst::FrameScope &itemsFrame) {
                    auto *layout = itemsFrame.component.addExtension<Amethyst::UIListLayout>();
                    layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
                    layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
                    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_TOP;
                    layout->innerPadding = Amethyst::UDim::fromOffset(ITEM_GAP);

                    Amethyst::UIScope itemScope(itemsFrame.component);
                    uint32_t itemOrder = 0;
                    for (const auto &bm : items) {
                        s_addBookmark(itemScope, itemOrder++, bm, navigate);
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

            auto navigate = [this](const std::filesystem::path &p) { navigateTo(p); };

            const char *home = std::getenv("HOME");
            std::filesystem::path homePath = (home != nullptr) ? std::filesystem::path(home) : std::filesystem::path();

            s_addSection(side, 0, "FAVORITES",
                         {{Icons::SVG_PIN, "Project Root", std::filesystem::current_path(), false}}, navigate);
            if (!homePath.empty()) {
                s_addSection(side, 1, "SYSTEM",
                             {
                                 {Icons::SVG_FOLDER, "Home", homePath, false},
                                 {Icons::SVG_FOLDER, "Desktop", homePath / "Desktop", false},
                                 {Icons::SVG_FOLDER, "Documents", homePath / "Documents", false},
                                 {Icons::SVG_FOLDER, "Downloads", homePath / "Downloads", false},
                             },
                             navigate);
            }
            s_addSection(side, 2, "PROJECT",
                         {{Icons::SVG_FOLDER, m_currentDirectory.filename().string(), m_currentDirectory, true}}, navigate);
            s_addSection(side, 3, "RECENT", {{Icons::SVG_SCRIPT, "scene.glb", {}, false}}, navigate);
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
                    .selectedRowColor = COL_SELECTED_4,
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

            m_table->onRowSelected = [this](uint32_t row) { onRowClicked(row); };
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

            // filename field (flex). Editable only when saving; otherwise it just mirrors the selection.
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
                            .placeholder = m_mode == Mode::SAVE ? "Filename" : "No file selected",
                        },
                        [this](Amethyst::TextInputScope &ti) {
                            m_filenameInput = &ti.component;
                            if (m_mode == Mode::OPEN) {
                                m_filenameInput->setBaseProperties({.interactable = false});
                            }
                        });
                });

            // filter dropdown (popup opens upward since the footer sits at the bottom)
            foot.dropdown(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                            .position = Amethyst::UDim2(1.0f, -(CONTENT_PADDING + openWidth + gap + cancelWidth + gap), 0.5f, 0.0f),
                            .size = Amethyst::UDim2(0.0f, filterWidth, 0.0f, 30.0f),
                        },
                    .style = {.backgroundColor = COL_PANEL_2, .cornerRadius = 3.0f},
                    .text =
                        {
                            .fontSize = 12.0f,
                            .textColor = COL_TEXT,
                            .textXAlignment = Amethyst::TextXAlignment::LEFT,
                            .textYAlignment = Amethyst::TextYAlignment::CENTER,
                        },
                    .label = "All Assets",
                    .dropdown = {.popupDirection = Amethyst::DropdownDirection::UP},
                },
                [this](Amethyst::DropdownScope &dd) {
                    Amethyst::Dropdown *box = &dd.component;

                    auto *caret = box->add<Amethyst::ImageLabel>();
                    caret->setBaseProperties({
                        .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                        .interactable = false,
                        .position = Amethyst::UDim2(1.0f, -8.0f, 0.5f, 0.0f),
                        .size = Amethyst::UDim2::fromOffset(12.0f, 12.0f),
                    });
                    caret->setBaseStyleProperties({.backgroundTransparency = 1.0f});
                    caret->setImageStyleProperties({.imageColor = COL_TEXT_DIM});
                    caret->setSvg(Icons::SVG_CARET_DOWN);

                    auto pick = [this, box](const char *label, std::vector<std::string> exts) {
                        box->setText(label);
                        m_extensionFilter = std::move(exts);
                        populate();
                    };
                    dd.action("All Assets", [pick] { pick("All Assets", {}); })
                        .action("Scenes", [pick] { pick("Scenes", {".gltf", ".glb"}); })
                        .action("Textures", [pick] { pick("Textures", {".png", ".jpg", ".jpeg", ".tga", ".ktx"}); })
                        .action("Scripts", [pick] { pick("Scripts", {".lua", ".cpp", ".h", ".hpp"}); });
                });

            const Amethyst::TextStyleProperties btnText{
                .fontSize = 12.0f,
                .textColor = COL_TEXT,
                .textXAlignment = Amethyst::TextXAlignment::CENTER,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
            };

            foot.textButton(
                {
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                            .position = Amethyst::UDim2(1.0f, -(CONTENT_PADDING + openWidth + gap), 0.5f, 0.0f),
                            .size = Amethyst::UDim2(0.0f, cancelWidth, 0.0f, 30.0f),
                        },
                    .style = {.backgroundColor = COL_PANEL_2, .cornerRadius = 3.0f},
                    .text = btnText,
                    .label = "Cancel",
                },
                [this](Amethyst::TextButtonScope &btn) {
                    btn.component.onMouseButton1ClickCb = [this]() {
                        if (onClose) {
                            onClose();
                        }
                        return Amethyst::EventResult::CONSUMED;
                    };
                });

            foot.textButton(
                {
                    .classes = {"primary"},
                    .base =
                        {
                            .anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                            .position = Amethyst::UDim2(1.0f, -CONTENT_PADDING, 0.5f, 0.0f),
                            .size = Amethyst::UDim2(0.0f, openWidth, 0.0f, 30.0f),
                        },
                    .style = {.cornerRadius = 3.0f},
                    .text = btnText,
                    .label = m_mode == Mode::SAVE ? "Save" : "Open",
                },
                [this](Amethyst::TextButtonScope &btn) {
                    btn.component.onMouseButton1ClickCb = [this]() {
                        if (m_selectedRow >= 0 && m_selectedRow < static_cast<int>(m_visibleEntries.size())) {
                            const Entry &e = m_visibleEntries[m_selectedRow];
                            if (e.isDir) {
                                navigateTo(e.path);
                            } else {
                                confirm(e.path);
                            }
                        } else if (m_mode == Mode::SAVE && m_filenameInput != nullptr) {
                            std::string name = m_filenameInput->getText();
                            if (!name.empty()) {
                                confirm(m_currentDirectory / name);
                            }
                        }
                        return Amethyst::EventResult::CONSUMED;
                    };
                });
        });
}

// One muted, single-aligned text cell filling its table cell.
static void s_textCell(Amethyst::UIScope &s, const std::string &text, const Amethyst::Color4 &color,
                       Amethyst::TextXAlignment align)
{
    s.textLabel({
        .base = {.interactable = false, .size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
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

void FileBrowser::readDirectory()
{
    m_allEntries.clear();

    std::error_code ec;
    if (!std::filesystem::exists(m_currentDirectory, ec)) {
        populate();
        return;
    }

    std::vector<std::filesystem::directory_entry> dir;
    for (std::filesystem::directory_iterator it(m_currentDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        dir.push_back(*it);
    }

    std::sort(dir.begin(), dir.end(), [](const std::filesystem::directory_entry &a, const std::filesystem::directory_entry &b) {
        bool aDir = a.is_directory();
        bool bDir = b.is_directory();
        if (aDir != bDir) {
            return aDir > bDir;
        }
        return a.path().filename().string() < b.path().filename().string();
    });

    for (const auto &entry : dir) {
        m_allEntries.push_back({entry.path(), entry.is_directory()});
    }

    populate();
}

void FileBrowser::populate()
{
    if (m_table == nullptr) {
        return;
    }

    m_table->clear();
    m_visibleEntries.clear();
    m_selectedRow = -1;
    updateSelectionLabel();

    Amethyst::TableScope ts(*m_table);
    for (const auto &entry : m_allEntries) {
        const std::string filename = entry.path.filename().string();

        if (!m_searchText.empty() && s_toLower(filename).find(m_searchText) == std::string::npos) {
            continue;
        }
        if (!entry.isDir && !m_extensionFilter.empty()) {
            std::string ext = s_toLower(entry.path.extension().string());
            if (std::find(m_extensionFilter.begin(), m_extensionFilter.end(), ext) == m_extensionFilter.end()) {
                continue;
            }
        }

        const bool isDir = entry.isDir;
        const std::string type = isDir ? "Folder" : (entry.path.has_extension() ? entry.path.extension().string() : "File");
        const std::string date = s_formatTime(entry.path);
        std::string sizeText = "—";
        if (!isDir) {
            std::error_code sizeEc;
            uintmax_t bytes = std::filesystem::file_size(entry.path, sizeEc);
            if (!sizeEc) {
                sizeText = s_formatSize(bytes);
            }
        }

        m_visibleEntries.push_back(entry);

        ts.row([=](Amethyst::TableRowScope &r) {
            // Name cell carries the icon inline (no dedicated icon column).
            r.cell([=](Amethyst::UIScope &s) {
                 s.imageLabel({
                     .base =
                         {
                             .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                             .interactable = false,
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
                             .interactable = false,
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
        m_statusLabel->setText(std::to_string(m_visibleEntries.size()) + " items");
    }
}

void FileBrowser::processDeferred()
{
    if (m_pendingNavigation.has_value()) {
        std::filesystem::path target = std::move(*m_pendingNavigation);
        m_pendingNavigation.reset();
        navigateTo(target);
    }
}

void FileBrowser::navigateTo(const std::filesystem::path &dir, bool recordHistory)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }

    if (recordHistory && dir != m_currentDirectory) {
        m_backStack.push_back(m_currentDirectory);
        m_forwardStack.clear();
    }

    m_currentDirectory = dir;
    if (m_pathInput != nullptr) {
        m_pathInput->setText(m_currentDirectory.string());
    }
    readDirectory();
    updateNavState();
}

void FileBrowser::goBack()
{
    if (m_backStack.empty()) {
        return;
    }
    m_forwardStack.push_back(m_currentDirectory);
    std::filesystem::path target = m_backStack.back();
    m_backStack.pop_back();
    navigateTo(target, false);
}

void FileBrowser::goForward()
{
    if (m_forwardStack.empty()) {
        return;
    }
    m_backStack.push_back(m_currentDirectory);
    std::filesystem::path target = m_forwardStack.back();
    m_forwardStack.pop_back();
    navigateTo(target, false);
}

void FileBrowser::goUp()
{
    std::filesystem::path parent = m_currentDirectory.parent_path();
    if (!parent.empty() && parent != m_currentDirectory) {
        navigateTo(parent);
    }
}

void FileBrowser::refresh()
{
    readDirectory();
}

void FileBrowser::updateNavState()
{
    applyNavEnabled(m_backButton, !m_backStack.empty());
    applyNavEnabled(m_forwardButton, !m_forwardStack.empty());
}

void FileBrowser::onRowClicked(uint32_t row)
{
    if (row >= m_visibleEntries.size()) {
        return;
    }

    const Entry &entry = m_visibleEntries[row];
    if (entry.isDir) {
        m_pendingNavigation = entry.path;
        return;
    }

    m_selectedRow = static_cast<int>(row);
    if (m_filenameInput != nullptr) {
        m_filenameInput->setText(entry.path.filename().string());
    }
    updateSelectionLabel();
}

void FileBrowser::updateSelectionLabel()
{
    if (m_selectionLabel == nullptr) {
        return;
    }
    m_selectionLabel->setText(m_selectedRow >= 0 ? "1 selected" : "");
}

void FileBrowser::confirm(const std::filesystem::path &path)
{
    if (onConfirm) {
        onConfirm(path);
    }
    if (onClose) {
        onClose();
    }
}
