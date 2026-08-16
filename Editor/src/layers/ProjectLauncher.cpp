#include "ProjectLauncher.h"

#include "Icons.h"
#include "core/utils/Log.h"

#include <components/extensions/ui_list_layout.h>
#include <components/image_label.h>
#include <components/properties.h>

static constexpr float CONTENT_PADDING = 24.0f;

static constexpr float TITLE_HEIGHT = 28.0f;
static constexpr float SUBTITLE_HEIGHT = 18.0f;
static constexpr float SECTION_LABEL_HEIGHT = 16.0f;
static constexpr float SECTION_GAP = 16.0f;
static constexpr float LIST_TOP = CONTENT_PADDING + TITLE_HEIGHT + SUBTITLE_HEIGHT + SECTION_GAP + SECTION_LABEL_HEIGHT;
static constexpr float LIST_PADDING = 6.0f;

static constexpr float FOOTER_HEIGHT = 30.0f;
static constexpr float FOOTER_GAP = 14.0f;
static constexpr float BUTTON_WIDTH = 84.0f;
static constexpr float BUTTON_GAP = 6.0f;

static constexpr float ROW_HEIGHT = 46.0f;
static constexpr float ROW_GAP = 4.0f;
static constexpr float ROW_ICON_SIZE = 18.0f;
static constexpr float ROW_ICON_LEFT = 14.0f;
static constexpr float ROW_TEXT_LEFT = 42.0f;
static constexpr float ROW_TEXT_RIGHT = 12.0f;
static constexpr float ROW_NAME_TOP = 7.0f;
static constexpr float ROW_NAME_HEIGHT = 18.0f;
static constexpr float ROW_PATH_TOP = 25.0f;
static constexpr float ROW_PATH_HEIGHT = 15.0f;

ProjectLauncher::ProjectLauncher(Amethyst::Instance &parent) : m_config(LauncherConfig::load())
{
    m_root = parent.add<Amethyst::Frame>();
    buildContent();
}

ProjectLauncher::~ProjectLauncher()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        m_root->parent->removeChild(m_root);
    }
}

void ProjectLauncher::buildContent()
{
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });

    m_root->name = "Project Launcher";
    m_root->addClass("launcher-backdrop");
    m_root->setBaseProperties({.position = Amethyst::UDim2::fromScale(0.0f), .size = Amethyst::UDim2::fromScale(1.0f)});

    Amethyst::UIScope root(*m_root);
    setupHeader(root);
    setupRecentList(root);
    setupFooter(root);
}

void ProjectLauncher::setupHeader(Amethyst::UIScope &root)
{
    root.textLabel({
        .classes = {"launcher-title"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(CONTENT_PADDING, CONTENT_PADDING),
                .size = Amethyst::UDim2(1.0f, -2.0f * CONTENT_PADDING, 0.0f, TITLE_HEIGHT),
            },
        .label = "Rapture Editor",
    });

    root.textLabel({
        .classes = {"launcher-subtitle"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(CONTENT_PADDING, CONTENT_PADDING + TITLE_HEIGHT),
                .size = Amethyst::UDim2(1.0f, -2.0f * CONTENT_PADDING, 0.0f, SUBTITLE_HEIGHT),
            },
        .label = "Open a recent project or create a new one",
    });
}

void ProjectLauncher::setupRecentList(Amethyst::UIScope &root)
{
    root.textLabel({
        .classes = {"launcher-section-label"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(CONTENT_PADDING, LIST_TOP - SECTION_LABEL_HEIGHT),
                .size = Amethyst::UDim2(1.0f, -2.0f * CONTENT_PADDING, 0.0f, SECTION_LABEL_HEIGHT),
            },
        .label = "RECENT PROJECTS",
    });

    const float listHeight = -(LIST_TOP + FOOTER_GAP + FOOTER_HEIGHT + CONTENT_PADDING);

    root.scrollingFrame(
        {
            .classes = {"launcher-list"},
            .base =
                {
                    .clipsDescendants = true,
                    .padding = {Amethyst::UDim::fromOffset(LIST_PADDING), Amethyst::UDim::fromOffset(LIST_PADDING),
                                Amethyst::UDim::fromOffset(LIST_PADDING), Amethyst::UDim::fromOffset(LIST_PADDING)},
                    .position = Amethyst::UDim2::fromOffset(CONTENT_PADDING, LIST_TOP),
                    .size = Amethyst::UDim2(1.0f, -2.0f * CONTENT_PADDING, 1.0f, listHeight),
                },
            .scroll =
                {
                    .scrollAxis = Amethyst::ScrollAxis::Y,
                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                    .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                },
        },
        [this](Amethyst::ScrollingFrameScope &sf) {
            if (m_config.recentProjects().empty()) {
                sf.textLabel({
                    .classes = {"launcher-list-empty"},
                    .base = {.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ROW_HEIGHT)},
                    .label = "No projects opened yet",
                });
                return;
            }

            auto *layout = sf.component.addExtension<Amethyst::UIListLayout>();
            layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
            layout->horizontalAlignment = Amethyst::HorizontalAlignment::ALIGN_LEFT;
            layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_TOP;
            layout->innerPadding = Amethyst::UDim::fromOffset(ROW_GAP);

            Amethyst::UIScope list(sf.component);
            int32_t order = 0;
            for (const auto &projectPath : m_config.recentProjects()) {
                addRecentRow(list, projectPath, order++);
            }
        });
}

void ProjectLauncher::addRecentRow(Amethyst::UIScope &list, const std::filesystem::path &projectPath, int32_t order)
{
    list.textButton(
        {
            .classes = {"launcher-project"},
            .base =
                {
                    .clipsDescendants = true,
                    .layoutOrder = order,
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, ROW_HEIGHT),
                },
        },
        [this, projectPath](Amethyst::TextButtonScope &row) {
            row.component.onMouseButton1ClickCb = [this, projectPath]() {
                if (onOpenProject) {
                    onOpenProject(projectPath);
                }
                return Amethyst::EventResult::CONSUMED;
            };

            auto *icon = row.component.add<Amethyst::ImageLabel>();
            icon->addClass("launcher-project-icon");
            icon->setBaseProperties({
                .anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                .interactable = false,
                .position = Amethyst::UDim2(0.0f, ROW_ICON_LEFT, 0.5f, 0.0f),
                .size = Amethyst::UDim2::fromOffset(ROW_ICON_SIZE, ROW_ICON_SIZE),
            });
            icon->setSvg(Icons::SVG_CUBE);

            row.textLabel({
                .classes = {"launcher-project-name"},
                .base =
                    {
                        .interactable = false,
                        .position = Amethyst::UDim2(0.0f, ROW_TEXT_LEFT, 0.0f, ROW_NAME_TOP),
                        .size = Amethyst::UDim2(1.0f, -(ROW_TEXT_LEFT + ROW_TEXT_RIGHT), 0.0f, ROW_NAME_HEIGHT),
                    },
                .text = {.textTruncate = Amethyst::TextTruncate::AT_END},
                .label = projectPath.stem().string(),
            });

            row.textLabel({
                .classes = {"launcher-project-path"},
                .base =
                    {
                        .interactable = false,
                        .position = Amethyst::UDim2(0.0f, ROW_TEXT_LEFT, 0.0f, ROW_PATH_TOP),
                        .size = Amethyst::UDim2(1.0f, -(ROW_TEXT_LEFT + ROW_TEXT_RIGHT), 0.0f, ROW_PATH_HEIGHT),
                    },
                .text = {.textTruncate = Amethyst::TextTruncate::AT_END},
                .label = projectPath.parent_path().string(),
            });
        });
}

void ProjectLauncher::setupFooter(Amethyst::UIScope &root)
{
    const float inputWidth = -(2.0f * BUTTON_WIDTH + 2.0f * BUTTON_GAP);

    root.frame(
        {
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.0f, 1.0f),
                    .position = Amethyst::UDim2(0.0f, CONTENT_PADDING, 1.0f, -CONTENT_PADDING),
                    .size = Amethyst::UDim2(1.0f, -2.0f * CONTENT_PADDING, 0.0f, FOOTER_HEIGHT),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this, inputWidth](Amethyst::FrameScope &footer) {
            footer.textInput(
                {
                    .classes = {"launcher-name-field"},
                    .base = {.size = Amethyst::UDim2(1.0f, inputWidth, 1.0f, 0.0f)},
                    .placeholder = "New project name",
                },
                [this](Amethyst::TextInputScope &input) { m_nameField = &input.component; });

            footer.textButton(
                {
                    .classes = {"launcher-primary-button"},
                    .base =
                        {
                            .position = Amethyst::UDim2(1.0f, -(2.0f * BUTTON_WIDTH + BUTTON_GAP), 0.0f, 0.0f),
                            .size = Amethyst::UDim2(0.0f, BUTTON_WIDTH, 1.0f, 0.0f),
                        },
                    .label = "Create",
                },
                [this](Amethyst::TextButtonScope &button) {
                    button.component.onMouseButton1ClickCb = [this]() {
                        createProject();
                        return Amethyst::EventResult::CONSUMED;
                    };
                });

            footer.textButton(
                {
                    .classes = {"generic-text-button"},
                    .base =
                        {
                            .position = Amethyst::UDim2(1.0f, -BUTTON_WIDTH, 0.0f, 0.0f),
                            .size = Amethyst::UDim2(0.0f, BUTTON_WIDTH, 1.0f, 0.0f),
                        },
                    .label = "Open...",
                },
                [this](Amethyst::TextButtonScope &button) {
                    button.component.onMouseButton1ClickCb = [this]() {
                        if (onBrowseForProject) {
                            onBrowseForProject();
                        }
                        return Amethyst::EventResult::CONSUMED;
                    };
                });
        });
}

void ProjectLauncher::createProject()
{
    if (m_nameField == nullptr) {
        return;
    }

    std::string name = m_nameField->getText();
    if (name.empty()) {
        RP_WARN("A project needs a name");
        return;
    }

    if (onCreateProject) {
        onCreateProject(name);
    }
}
