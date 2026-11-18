#include "layers/CommandLine.h"

#include <components/text_input.h>
#include <components/ui_scope.h>
#include <rapture/core/utils/Log.h>

#include <algorithm>

static constexpr float COMMAND_POPUP_WIDTH = 520.0f;
static constexpr float COMMAND_POPUP_ROW_HEIGHT = 26.0f;
static constexpr float COMMAND_POPUP_PADDING = 4.0f;
static constexpr float COMMAND_POPUP_TEXT_INSET = 10.0f;
static constexpr float COMMAND_POPUP_TEXT_COLUMN = 0.35f;
static constexpr size_t COMMAND_POPUP_MAX_ROWS = 14;

static constexpr std::string_view CLASS_POPUP = "command-popup";
static constexpr std::string_view CLASS_ROW = "command-popup-row";
static constexpr std::string_view CLASS_ROW_HIGHLIGHTED = "command-popup-row-highlighted";
static constexpr std::string_view CLASS_TEXT = "command-popup-text";
static constexpr std::string_view CLASS_DESCRIPTION = "command-popup-description";
static constexpr std::string_view CLASS_FAILED = "command-popup-failed";

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

static std::vector<std::string> s_splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

CommandLine::CommandLine(Amethyst::TextInput &input, Amethyst::Instance &popupParent, const Rapture::CommandRegistry &registry)
    : m_input(&input), m_registry(&registry)
{
    Amethyst::UIScope(popupParent)
        .popup(
            {
                .classes = {std::string(CLASS_POPUP)},
                .base = {.size = Amethyst::UDim2::fromOffset(COMMAND_POPUP_WIDTH, COMMAND_POPUP_ROW_HEIGHT)},
                .placement = Amethyst::PopupPlacement::ABOVE,
            },
            [this](Amethyst::PopupScope &popup) { m_popup = &popup.component; });

    m_popup->onClosed = [this]() { m_popupContent = COMMAND_POPUP_CONTENT_CLOSED; };

    m_input->onEnterPressed = [this]() { run(); };
    m_input->onTextChanged = [this](const std::string &text) {
        (void)text;
        refreshCompletions();
    };
    m_input->interceptKey = [this](const Amethyst::KeyEvent &event) { return interceptKey(event); };
}

bool CommandLine::interceptKey(const Amethyst::KeyEvent &event)
{
    if (event.action == Amethyst::KEY_ACTION_RELEASE) {
        return false;
    }

    const bool listing = m_popupContent == COMMAND_POPUP_CONTENT_COMPLETIONS;
    switch (event.key) {
    case Amethyst::KEY_TAB:
        if (listing) {
            acceptCompletion();
        } else {
            refreshCompletions();
        }
        return true;
    case Amethyst::KEY_UP:
        if (listing) {
            moveHighlight(-1);
        }
        return listing;
    case Amethyst::KEY_DOWN:
        if (listing) {
            moveHighlight(1);
        }
        return listing;
    case Amethyst::KEY_ESCAPE:
        if (m_popupContent == COMMAND_POPUP_CONTENT_CLOSED) {
            return false;
        }
        closePopup();
        return true;
    default:
        return false;
    }
}

void CommandLine::run()
{
    const std::string line = m_input->getText();
    if (line.empty()) {
        return;
    }

    m_input->clearText();
    RP_INFO("> {}", line);
    m_registry->execute(line, Rapture::CommandReply([this, line](Rapture::CommandResult result, std::string text) {
                            showAnswer(line, result, text);
                        }));
}

void CommandLine::refreshCompletions()
{
    const std::string line = m_input->getText();
    m_completions = line.empty() ? std::vector<Rapture::CommandCompletion>{} : m_registry->complete(line);
    m_highlightedCompletion = 0;

    if (m_completions.empty()) {
        closePopup();
        return;
    }

    showCompletions();
}

void CommandLine::acceptCompletion()
{
    if (m_completions.empty()) {
        return;
    }

    const std::string line = m_input->getText();
    const std::string head = line.substr(0, s_lastWordStart(line));
    m_input->setText(head + s_quoteIfSpaced(m_completions[m_highlightedCompletion].text) + " ");
    m_input->moveCursorToEnd();

    // a programmatic edit does not report itself, and the next word has its own candidates
    refreshCompletions();
}

void CommandLine::moveHighlight(int step)
{
    const int count = static_cast<int>(m_completions.size());
    m_highlightedCompletion = static_cast<size_t>((static_cast<int>(m_highlightedCompletion) + step + count) % count);
    showCompletions();
}

void CommandLine::showCompletions()
{
    const size_t shown = std::min(m_completions.size(), COMMAND_POPUP_MAX_ROWS);
    const size_t first = m_highlightedCompletion >= shown ? m_highlightedCompletion - shown + 1 : 0;

    resetPopup(shown);
    for (size_t row = 0; row < shown; row++) {
        const size_t index = first + row;
        const Rapture::CommandCompletion &completion = m_completions[index];

        Amethyst::UIScope(*m_popup).frame(
            {
                .classes = {std::string(index == m_highlightedCompletion ? CLASS_ROW_HIGHLIGHTED : CLASS_ROW)},
                .base =
                    {
                        .position = Amethyst::UDim2::fromOffset(0.0f, COMMAND_POPUP_PADDING + static_cast<float>(row) * COMMAND_POPUP_ROW_HEIGHT),
                        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, COMMAND_POPUP_ROW_HEIGHT),
                    },
            },
            [&completion](Amethyst::FrameScope &rowScope) {
                rowScope.textLabel({
                    .classes = {std::string(CLASS_TEXT)},
                    .base =
                        {
                            .position = Amethyst::UDim2::fromOffset(COMMAND_POPUP_TEXT_INSET, 0.0f),
                            .size = Amethyst::UDim2(COMMAND_POPUP_TEXT_COLUMN, -COMMAND_POPUP_TEXT_INSET, 1.0f, 0.0f),
                        },
                    .label = completion.text,
                });
                rowScope.textLabel({
                    .classes = {std::string(CLASS_DESCRIPTION)},
                    .base =
                        {
                            .position = Amethyst::UDim2(COMMAND_POPUP_TEXT_COLUMN, 0.0f, 0.0f, 0.0f),
                            .size = Amethyst::UDim2(1.0f - COMMAND_POPUP_TEXT_COLUMN, -COMMAND_POPUP_TEXT_INSET, 1.0f, 0.0f),
                        },
                    .label = completion.description,
                });
            });
    }

    m_popupContent = COMMAND_POPUP_CONTENT_COMPLETIONS;
    openPopup();
}

void CommandLine::showAnswer(const std::string &line, Rapture::CommandResult result, const std::string &text)
{
    const bool failed = result == Rapture::COMMAND_RESULT_FAILED;
    if (failed) {
        RP_WARN("{}", text);
    } else {
        RP_INFO("{}", text);
    }

    std::vector<std::string> lines = s_splitLines(text);
    if (lines.empty()) {
        lines.push_back(failed ? "failed" : "done");
    }

    // the command itself takes the first row, and an answer too long to show ends in a pointer to the log
    const size_t room = COMMAND_POPUP_MAX_ROWS - 1;
    const bool truncated = lines.size() > room;
    const size_t shownLines = truncated ? room - 1 : lines.size();

    resetPopup(1 + shownLines + (truncated ? 1 : 0));
    addPopupLine(0, "> " + line, CLASS_DESCRIPTION);
    for (size_t i = 0; i < shownLines; i++) {
        addPopupLine(1 + i, lines[i], failed ? CLASS_FAILED : CLASS_TEXT);
    }
    if (truncated) {
        addPopupLine(1 + shownLines, std::to_string(lines.size() - shownLines) + " more lines in the log", CLASS_DESCRIPTION);
    }

    m_popupContent = COMMAND_POPUP_CONTENT_ANSWER;
    openPopup();
}

void CommandLine::closePopup()
{
    m_popup->close();
    m_popupContent = COMMAND_POPUP_CONTENT_CLOSED;
}

void CommandLine::resetPopup(size_t rowCount)
{
    while (!m_popup->getChildren().empty()) {
        m_popup->removeChild(m_popup->getChildren().back().get());
    }

    const float height = 2.0f * COMMAND_POPUP_PADDING + static_cast<float>(rowCount) * COMMAND_POPUP_ROW_HEIGHT;
    m_popup->setBaseProperties({.size = Amethyst::UDim2::fromOffset(COMMAND_POPUP_WIDTH, height)});
}

void CommandLine::addPopupLine(size_t row, const std::string &text, std::string_view textClass)
{
    Amethyst::UIScope(*m_popup).textLabel({
        .classes = {std::string(textClass)},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(COMMAND_POPUP_TEXT_INSET, COMMAND_POPUP_PADDING + static_cast<float>(row) * COMMAND_POPUP_ROW_HEIGHT),
                .size = Amethyst::UDim2(1.0f, -2.0f * COMMAND_POPUP_TEXT_INSET, 0.0f, COMMAND_POPUP_ROW_HEIGHT),
            },
        .label = text,
    });
}

void CommandLine::openPopup()
{
    // reopening places it again for its new height
    m_popup->open(m_input);
}
