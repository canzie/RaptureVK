#include "AddComponentWindow.h"

#include "Icons.h"
#include "ui/AssetVisuals.h"
#include "ui/controls/IconButton.h"
#include "ui/controls/SearchField.h"

#include <rapture/scene/instances/InstanceRegistry.h>
#include <rapture/scene/instances/SceneObject.h>

#include <components/extensions/ui_drag_detector.h>
#include <components/extensions/ui_flex_item.h>
#include <components/extensions/ui_list_layout.h>
#include <components/window.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

static constexpr float WINDOW_WIDTH = 560.0f;
static constexpr float WINDOW_TOP_FRACTION = 0.28f;
static constexpr float TITLE_BAR_HEIGHT = 38.0f;
static constexpr float TITLE_PADDING = 12.0f;
static constexpr float TITLE_TARGET_GAP = 8.0f;
static constexpr float CLOSE_BUTTON_SIZE = 22.0f;
static constexpr float CLOSE_ICON_SIZE = 12.0f;
static constexpr float BODY_PADDING = 10.0f;
static constexpr float BODY_GAP = 8.0f;
static constexpr float SEARCH_HEIGHT = 26.0f;
static constexpr float ROW_HEIGHT = 30.0f;
static constexpr float ROW_PADDING = 8.0f;
static constexpr float ROW_GAP = 10.0f;
static constexpr float ROW_ICON_SIZE = 14.0f;
static constexpr float ROW_NAME_WIDTH = 130.0f;
static constexpr float EXPAND_BUTTON_SIZE = 22.0f;
static constexpr float EXPAND_ICON_SIZE = 11.0f;
static constexpr float DETAILS_INDENT = 32.0f;
static constexpr float DETAILS_PADDING = 8.0f;
static constexpr float DIVIDER_THICKNESS = 1.0f;

static constexpr std::string_view WINDOW_CLASS = "add-component-window";
static constexpr std::string_view TITLE_CLASS = "add-component-title";
static constexpr std::string_view TARGET_CLASS = "add-component-target";
static constexpr std::string_view CLOSE_BUTTON_CLASS = "add-component-close";
static constexpr std::string_view DIVIDER_CLASS = "add-component-divider";
static constexpr std::string_view ROW_CLASS = "add-component-row";
static constexpr std::string_view ROW_ICON_CLASS = "add-component-row-icon";
static constexpr std::string_view ROW_NAME_CLASS = "add-component-row-name";
static constexpr std::string_view ROW_SUMMARY_CLASS = "add-component-row-summary";
static constexpr std::string_view ATTACHED_SUFFIX = "-attached";
static constexpr std::string_view EXPAND_BUTTON_CLASS = "add-component-expand";
static constexpr std::string_view DETAILS_CLASS = "add-component-details";

static std::string s_toLowercase(std::string_view text)
{
    std::string lowered(text);
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

static bool s_hasComponentOfClass(const Rapture::SceneObject &object, const Rapture::TypeInfo &type)
{
    return std::ranges::any_of(object.components(), [&type](const auto &component) { return &component->type() == &type; });
}

static void s_ignoreChildInput(Amethyst::UIObject &object)
{
    for (const auto &child : object.getChildren()) {
        if (auto *childObject = child->asUiObject()) {
            childObject->ignore(Amethyst::INTERACTION_CATEGORY_ALL);
        }
    }
}

AddComponentWindow::AddComponentWindow(Amethyst::Frame &parent)
{
    Amethyst::UIScope(parent).popup(
        {
            .classes = {std::string(WINDOW_CLASS)},
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::Y,
                    .size = Amethyst::UDim2::fromOffset(WINDOW_WIDTH, 0.0f),
                },
        },
        [this](Amethyst::PopupScope &p) {
            m_window = &p.component;
            auto *stack = m_window->addExtension<Amethyst::UIListLayout>();
            stack->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;

            buildTitleBar(p);
            buildBody(p);
        });

    m_window->addExtension<Amethyst::UIDragDetector>(m_titleBar);
    m_windowDestroyConn = m_window->onDestroy.connect([this](Amethyst::Instance *) {
        m_window = nullptr;
        m_titleBar = nullptr;
        m_targetLabel = nullptr;
        m_search = nullptr;
        m_rows = nullptr;
    });
}

void AddComponentWindow::buildTitleBar(Amethyst::UIScope &window)
{
    window.frame(
        {
            .base =
                {
                    .layoutOrder = 0,
                    .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(BODY_PADDING),
                                               .left = Amethyst::UDim::fromOffset(TITLE_PADDING)},
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, TITLE_BAR_HEIGHT),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &bar) {
            m_titleBar = &bar.component;
            auto *row = m_titleBar->addExtension<Amethyst::UIListLayout>();
            row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
            row->horizontalFlex = Amethyst::UiFlexAlignment::FILL;
            row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
            row->innerPadding = Amethyst::UDim::fromOffset(TITLE_TARGET_GAP);

            bar.textLabel({
                .classes = {std::string(TITLE_CLASS)},
                .base = {.automaticSize = Amethyst::AutomaticSize::X,
                         .interactable = false,
                         .layoutOrder = 0,
                         .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                .label = "Add Component",
            });
            bar.textLabel(
                {
                    .classes = {std::string(TARGET_CLASS)},
                    .base = {.interactable = false, .layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                    .text = {.textTruncate = Amethyst::TextTruncate::ELLIPSIS},
                },
                [this](Amethyst::TextLabelScope &label) {
                    m_targetLabel = &label.component;
                    m_targetLabel->addExtension<Amethyst::UIFlexItem>();
                });

            auto *closeButton = m_titleBar->add<IconButton>(Icons::SVG_X, CLOSE_ICON_SIZE, std::vector<std::string>{std::string(CLOSE_BUTTON_CLASS)});
            closeButton->setBaseProperties({.layoutOrder = 2, .size = Amethyst::UDim2::fromOffset(CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE)});
            closeButton->track(closeButton->onMouseButton1ClickCb.connect([this]() { close(); }));
        });

    window.frame({
        .classes = {std::string(DIVIDER_CLASS)},
        .base = {.interactable = false, .layoutOrder = 1, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, DIVIDER_THICKNESS)},
    });
}

void AddComponentWindow::buildBody(Amethyst::UIScope &window)
{
    window.frame(
        {
            .base =
                {
                    .automaticSize = Amethyst::AutomaticSize::Y,
                    .layoutOrder = 2,
                    .padding = Amethyst::UDim4::fromOffset(BODY_PADDING),
                    .size = Amethyst::UDim2::fromScale(1.0f, 0.0f),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &body) {
            auto *stack = body.component.addExtension<Amethyst::UIListLayout>();
            stack->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
            stack->innerPadding = Amethyst::UDim::fromOffset(BODY_GAP);

            m_search = body.component.add<SearchField>("Search components");
            m_search->setBaseProperties({.layoutOrder = 0, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, SEARCH_HEIGHT)});
            m_search->setOnTextChanged([this](const std::string &text) { fillRows(text); });
            m_search->setOnEscapePressed([this]() { close(); });

            body.frame(
                {
                    .base = {.automaticSize = Amethyst::AutomaticSize::Y, .layoutOrder = 1, .size = Amethyst::UDim2::fromScale(1.0f, 0.0f)},
                    .style = {.backgroundTransparency = 1.0f},
                },
                [this](Amethyst::FrameScope &rows) {
                    m_rows = &rows.component;
                    auto *list = m_rows->addExtension<Amethyst::UIListLayout>();
                    list->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
                });
        });
}

void AddComponentWindow::open(Rapture::SceneObject &target)
{
    if (m_window == nullptr) {
        return;
    }

    m_target = &target;
    m_targetDestroyConn = target.onDestroy.connect([this](Rapture::Instance *) { close(); });
    m_targetLabel->setText("to " + std::string(target.name()));

    m_search->clear();
    fillRows({});

    Amethyst::vec2 position(0.0f);
    if (const Amethyst::Window *window = m_window->getWindow()) {
        position = Amethyst::vec2((window->absoluteSize.x - WINDOW_WIDTH) * 0.5f, window->absoluteSize.y * WINDOW_TOP_FRACTION);
    }
    m_window->openAt(position);
    m_search->focus();
}

void AddComponentWindow::close()
{
    m_target = nullptr;
    m_targetDestroyConn = {};

    if (m_window != nullptr && m_window->isOpen()) {
        m_window->close();
    }
}

void AddComponentWindow::fillRows(std::string_view search)
{
    if (m_rows == nullptr) {
        return;
    }

    std::vector<std::unique_ptr<Amethyst::Instance>> previous;
    while (!m_rows->getChildren().empty()) {
        previous.push_back(m_rows->removeChild(m_rows->getChildren().back().get()));
    }

    m_loweredSearch = s_toLowercase(search);

    uint32_t order = 0;
    Amethyst::UIScope rows(*m_rows);
    for (const Rapture::TypeInfo *type : Rapture::InstanceRegistry::componentClasses()) {
        const SceneComponentDescription *description = SceneComponent_describeClass(type->name);
        const std::string_view name = description != nullptr ? description->name : type->name;
        if (s_toLowercase(name).find(m_loweredSearch) == std::string::npos) {
            continue;
        }
        buildRow(rows, *type, order++);
    }
}

void AddComponentWindow::buildRow(Amethyst::UIScope &rows, const Rapture::TypeInfo &type, uint32_t order)
{
    const SceneComponentDescription *description = SceneComponent_describeClass(type.name);
    const bool attached = m_target != nullptr && s_hasComponentOfClass(*m_target, type);

    const std::string name(description != nullptr ? description->name : type.name);
    const std::string summary(attached ? std::string_view("Already attached") : (description != nullptr ? description->summary : std::string_view{}));
    const std::string_view details = description != nullptr ? description->details : std::string_view{};
    const char *svg = description != nullptr ? description->svg : Icons::SVG_CUBE;
    const std::string_view className = type.name;

    auto rowClasses = [attached](std::string_view styleClass) {
        return std::vector<std::string>{attached ? std::string(styleClass) + std::string(ATTACHED_SUFFIX) : std::string(styleClass)};
    };

    rows.frame(
        {
            .base = {.automaticSize = Amethyst::AutomaticSize::Y, .layoutOrder = order, .size = Amethyst::UDim2::fromScale(1.0f, 0.0f)},
            .style = {.backgroundTransparency = 1.0f},
        },
        [&](Amethyst::FrameScope &entry) {
            auto *stack = entry.component.addExtension<Amethyst::UIListLayout>();
            stack->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;

            Amethyst::TextLabel *detailsLabel = nullptr;
            IconButton *expandButton = nullptr;

            entry.textButton(
                {
                    .classes = {std::string(ROW_CLASS)},
                    .base = {.layoutOrder = 0,
                             .padding = Amethyst::UDim4{.right = Amethyst::UDim::fromOffset(ROW_PADDING),
                                                        .left = Amethyst::UDim::fromOffset(ROW_PADDING)},
                             .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ROW_HEIGHT)},
                },
                [&](Amethyst::TextButtonScope &row) {
                    auto *layout = row.component.addExtension<Amethyst::UIListLayout>();
                    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                    layout->horizontalFlex = Amethyst::UiFlexAlignment::FILL;
                    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                    layout->innerPadding = Amethyst::UDim::fromOffset(ROW_GAP);

                    row.imageLabel({
                        .classes = rowClasses(ROW_ICON_CLASS),
                        .base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(ROW_ICON_SIZE, ROW_ICON_SIZE)},
                        .svg = svg,
                    });
                    row.textLabel({
                        .classes = rowClasses(ROW_NAME_CLASS),
                        .base = {.layoutOrder = 1, .size = Amethyst::UDim2(0.0f, ROW_NAME_WIDTH, 1.0f, 0.0f)},
                        .label = name,
                    });
                    row.textLabel(
                        {
                            .classes = rowClasses(ROW_SUMMARY_CLASS),
                            .base = {.layoutOrder = 2, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)},
                            .text = {.textTruncate = Amethyst::TextTruncate::ELLIPSIS},
                            .label = summary,
                        },
                        [](Amethyst::TextLabelScope &label) { label.component.addExtension<Amethyst::UIFlexItem>(); });
                    s_ignoreChildInput(row.component);

                    if (!details.empty()) {
                        expandButton = row.component.add<IconButton>(Icons::SVG_CARET_RIGHT, EXPAND_ICON_SIZE,
                                                                     std::vector<std::string>{std::string(EXPAND_BUTTON_CLASS)});
                        expandButton->setBaseProperties({.layoutOrder = 3, .size = Amethyst::UDim2::fromOffset(EXPAND_BUTTON_SIZE, EXPAND_BUTTON_SIZE)});
                    }

                    if (!attached) {
                        row.component.track(row.component.onMouseButton1ClickCb.connect([this, className]() { addComponentToTarget(className); }));
                    }
                });

            if (details.empty()) {
                return;
            }

            entry.textLabel(
                {
                    .classes = {std::string(DETAILS_CLASS)},
                    .base = {.automaticSize = Amethyst::AutomaticSize::Y,
                             .interactable = false,
                             .layoutOrder = 1,
                             .padding = Amethyst::UDim4{.top = Amethyst::UDim::fromOffset(DETAILS_PADDING * 0.5f),
                                                        .right = Amethyst::UDim::fromOffset(DETAILS_PADDING),
                                                        .bottom = Amethyst::UDim::fromOffset(DETAILS_PADDING),
                                                        .left = Amethyst::UDim::fromOffset(DETAILS_INDENT)},
                             .size = Amethyst::UDim2::fromScale(1.0f, 0.0f),
                             .visible = false},
                    .text = {.textWrapped = true},
                    .label = std::string(details),
                },
                [&](Amethyst::TextLabelScope &label) { detailsLabel = &label.component; });

            expandButton->track(expandButton->onMouseButton1ClickCb.connect([expandButton, detailsLabel]() {
                const bool open = !static_cast<bool>(detailsLabel->getBaseProperties().visible);
                detailsLabel->setBaseProperties({.visible = open});
                expandButton->setIcon(open ? Icons::SVG_CHEVRON_DOWN : Icons::SVG_CARET_RIGHT);
            }));
        });
}

void AddComponentWindow::addComponentToTarget(std::string_view className)
{
    Rapture::SceneObject *target = m_target;
    if (target == nullptr || target->scene() == nullptr) {
        return;
    }

    std::unique_ptr<Rapture::SceneComponent> component = Rapture::InstanceRegistry::createComponent(className, *target->scene(), className);
    Rapture::SceneComponent *added = target->addComponent(std::move(component));
    if (added != nullptr) {
        added->ready();
    }

    close();

    if (added != nullptr && onComponentAdded != nullptr) {
        onComponentAdded();
    }
}
