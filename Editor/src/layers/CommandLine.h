#ifndef RAPTURE__COMMAND_LINE_H
#define RAPTURE__COMMAND_LINE_H

#include <amethyst/Amethyst.h>
#include <components/input_events.h>
#include <components/popup.h>
#include <rapture/core/commands/CommandRegistry.h>

#include <string>
#include <vector>

/**
 * @brief What the popup above the command line is showing
 */
enum CommandPopupContent {
    COMMAND_POPUP_CONTENT_CLOSED,
    COMMAND_POPUP_CONTENT_COMPLETIONS,
    COMMAND_POPUP_CONTENT_ANSWER
};

/**
 * @brief A text field commands are typed into, with a popup above it listing completions and showing answers
 */
class CommandLine {
  public:
    /**
     * @brief Takes over a text field and adds the popup it opens
     * @param input The field commands are typed into, which has to outlive this
     * @param popupParent What the popup is added under
     * @param registry The commands that are run and completed, which has to outlive this
     */
    CommandLine(Amethyst::TextInput &input, Amethyst::Instance &popupParent, const Rapture::CommandRegistry &registry);

    CommandLine(const CommandLine &) = delete;
    CommandLine &operator=(const CommandLine &) = delete;

  private:
    /**
     * @brief Handles the keys the popup answers to, before the field sees them
     * @param event The key
     * @return True if the key was handled here
     */
    bool interceptKey(const Amethyst::KeyEvent &event);

    /**
     * @brief Runs what the field holds, clears it and shows the answer
     */
    void run();

    /**
     * @brief Lists what the word being typed can become, closing the popup when nothing can
     */
    void refreshCompletions();

    /**
     * @brief Replaces the word being typed with the highlighted completion
     */
    void acceptCompletion();

    /**
     * @brief Moves the highlight through the completions, wrapping at either end
     * @param step +1 for the next completion, -1 for the previous one
     */
    void moveHighlight(int step);

    /**
     * @brief Shows a command's answer in the popup and logs all of it
     * @param line The command that was run
     * @param result How it ended
     * @param text The answer
     */
    void showAnswer(const std::string &line, Rapture::CommandResult result, const std::string &text);

    void showCompletions();
    void closePopup();

    /**
     * @brief Empties the popup and sizes it for a number of rows
     * @param rowCount How many rows it will hold
     */
    void resetPopup(size_t rowCount);

    /**
     * @brief Adds one line of text to the popup
     * @param row Which row it is, from the top
     * @param text The text
     * @param textClass The class the text is drawn with
     */
    void addPopupLine(size_t row, const std::string &text, std::string_view textClass);

    void openPopup();

  private:
    Amethyst::TextInput *m_input;
    Amethyst::Popup *m_popup = nullptr;
    const Rapture::CommandRegistry *m_registry;

    CommandPopupContent m_popupContent = COMMAND_POPUP_CONTENT_CLOSED;
    std::vector<Rapture::CommandCompletion> m_completions;
    size_t m_highlightedCompletion = 0;
};

#endif // RAPTURE__COMMAND_LINE_H
