#include "layers/BottomBar.h"

#include "app/EditorConfig.h"
#include "commands/EditorCommands.h"
#include "panels/ContentBrowserPanel.h"

#include <rapture/core/utils/Log.h>

#include <algorithm>

#include <components/common.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <modules/color.h>

static constexpr float BOTTOM_BAR_PADDING = 4.0f;
static constexpr float CONTENT_BROWSER_BUTTON_WIDTH = 150.0f;
static constexpr float SEARCHBAR_CONTAINER_WIDTH = 400.0f;
static constexpr float COMMAND_ANSWER_PADDING = 12.0f;
static constexpr std::string_view COMMAND_ANSWER_FAILED_CLASS = "command-answer-failed";

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
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH + 4.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, SEARCHBAR_CONTAINER_WIDTH, 1.0f, 0.0f),
                },
        },
        [this](Amethyst::FrameScope &cont) {
            cont.component.addClass("bottom-bar");
            cont.frame(
                {
                    .base =
                        {
                            .anchorPoint = {0.5f, 0.5f},
                            .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
                            .size = Amethyst::UDim2::fromScale(0.95f, 0.8f),
                        },

                },
                [this](Amethyst::FrameScope &sb) {
                    sb.textInput({.base = {.size = Amethyst::UDim2::fromScale(1.0f)}, .placeholder = "COMMAND"},
                                 [this](Amethyst::TextInputScope &tis) {
                                     tis.component.addClass("searchbar");
                                     m_commandInput = &tis.component;
                                 });
                });
        });
    rootScope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH, 0.0f),
                    .size = Amethyst::UDim2(0.0f, 2.0f, 1.0f, 0.0f),
                },
            .style = {.backgroundColor = Amethyst::Color3::fromHex(0x181818)},
        },
        [](Amethyst::FrameScope &fs) { (void)fs; });

    rootScope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(CONTENT_BROWSER_BUTTON_WIDTH + SEARCHBAR_CONTAINER_WIDTH + 2.0f, 0.0f),
                    .size = Amethyst::UDim2(0.0f, 2.0f, 1.0f, 0.0f),
                },
            .style = {.backgroundColor = Amethyst::Color3::fromHex(0x181818)},
        },
        [](Amethyst::FrameScope &fs) { (void)fs; });

    setupCommandLine(rootScope);
    setupContentBrowserToggle();
}

/**
 * @brief Where the word a command line ends in starts, a quoted word counting as one
 * @param line The command line
 * @return The index of the word's first character, the line's length when it ends in a space
 */
static size_t s_lastWordStart(std::string_view line)
{
    size_t start = 0;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '"') {
            inQuotes = !inQuotes;
        } else if (!inQuotes && (line[i] == ' ' || line[i] == '\t')) {
            start = i + 1;
        }
    }
    return start;
}

static std::string s_quoteIfSpaced(const std::string &word)
{
    return word.find(' ') == std::string::npos ? word : "\"" + word + "\"";
}

static std::string s_commonPrefix(const std::vector<std::string> &words)
{
    std::string prefix = words.front();
    for (const std::string &word : words) {
        size_t shared = 0;
        while (shared < prefix.size() && shared < word.size() && prefix[shared] == word[shared]) {
            shared++;
        }
        prefix.resize(shared);
    }
    return prefix;
}

void BottomBar::setupCommandLine(Amethyst::UIScope &root)
{
    const float answerLeft = CONTENT_BROWSER_BUTTON_WIDTH + SEARCHBAR_CONTAINER_WIDTH + 2.0f + COMMAND_ANSWER_PADDING;
    root.textLabel(
        {
            .classes = {"command-answer"},
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(answerLeft, 0.0f),
                    .size = Amethyst::UDim2(1.0f, -answerLeft - COMMAND_ANSWER_PADDING, 1.0f, 0.0f),
                },
        },
        [this](Amethyst::TextLabelScope &label) { m_commandAnswer = &label.component; });

    m_commandInput->onEnterPressed = [this]() { runCommandLine(); };
    m_commandInput->onTabPressed = [this]() { completeCommandLine(); };
}

void BottomBar::runCommandLine()
{
    const std::string line = m_commandInput->getText();
    if (line.empty() || m_editor.commands == nullptr) {
        return;
    }

    m_commandInput->clearText();
    RP_INFO("> {}", line);
    m_editor.commands->registry().execute(
        line, Rapture::CommandReply([this](Rapture::CommandResult result, std::string text) { showCommandAnswer(result, text); }));
}

void BottomBar::completeCommandLine()
{
    if (m_editor.commands == nullptr) {
        return;
    }

    const std::string line = m_commandInput->getText();
    std::vector<std::string> candidates = m_editor.commands->registry().complete(line);
    if (candidates.empty()) {
        showCommandAnswer(Rapture::COMMAND_RESULT_FAILED, "nothing to complete");
        return;
    }

    const std::string head = line.substr(0, s_lastWordStart(line));
    if (candidates.size() == 1) {
        m_commandInput->setText(head + s_quoteIfSpaced(candidates.front()) + " ");
        m_commandInput->moveCursorToEnd();
        return;
    }

    m_commandInput->setText(head + s_commonPrefix(candidates));
    m_commandInput->moveCursorToEnd();

    std::string listing;
    for (const std::string &candidate : candidates) {
        listing += (listing.empty() ? "" : "   ") + candidate;
    }
    showCommandAnswer(Rapture::COMMAND_RESULT_SUCCEEDED, listing);
}

void BottomBar::showCommandAnswer(Rapture::CommandResult result, const std::string &text)
{
    if (result == Rapture::COMMAND_RESULT_FAILED) {
        RP_WARN("{}", text);
        m_commandAnswer->addClass(COMMAND_ANSWER_FAILED_CLASS);
    } else {
        RP_INFO("{}", text);
        m_commandAnswer->removeClass(COMMAND_ANSWER_FAILED_CLASS);
    }

    const size_t firstLineEnd = text.find('\n');
    std::string shown = text.substr(0, firstLineEnd);
    const size_t lineCount = static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + (text.ends_with('\n') ? 0 : 1);
    if (lineCount > 1) {
        shown += "   (" + std::to_string(lineCount - 1) + " more lines in the log)";
    }
    m_commandAnswer->setText(shown);
}

BottomBar::~BottomBar() = default;

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
