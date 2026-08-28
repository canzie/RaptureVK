#ifndef RAPTURE__SHORTCUT_REGISTRY_H
#define RAPTURE__SHORTCUT_REGISTRY_H

#include <amethyst/Amethyst.h>

#include <input/Input.h>
#include <input/InputCodes.h>

#include <array>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

enum EditorCommand {
    EDITOR_COMMAND_EXIT,
    EDITOR_COMMAND_PLAY_MODE_TOGGLE_CONTROL,
    EDITOR_COMMAND_PLAY_MODE_STOP,
    EDITOR_COMMAND_SAVE,
    EDITOR_COMMAND_COUNT
};

enum ShortcutModifier {
    SHORTCUT_MOD_NONE = 0,
    SHORTCUT_MOD_CONTROL = 1 << 0,
    SHORTCUT_MOD_SHIFT = 1 << 1,
    SHORTCUT_MOD_ALT = 1 << 2
};

/**
 * @brief The key and modifiers that run a command, whose key is zero when it runs on nothing.
 */
struct Shortcut {
    Rapture::KeyCode key = static_cast<Rapture::KeyCode>(0);
    uint8_t modifiers = SHORTCUT_MOD_NONE;

    bool isValid() const { return key != static_cast<Rapture::KeyCode>(0); }

    bool operator==(const Shortcut &) const = default;
};

/**
 * @brief The shortcuts currently standing for the editor's commands.
 *
 * Anywhere that can carry a command out hands the registry a handler, so several parts of the
 * editor can offer the same command and the one under the cursor is the one that runs. The keys
 * belong to the command rather than to any of them, so a command the user rebound keeps its keys
 * as panels offering it come and go.
 */
class ShortcutRegistry {
  public:
    /**
     * @brief Offers to run a command for as long as the UI behind it lives
     * @param command The command that can be run
     * @param defaultShortcut The keys that run it until the user says otherwise
     * @param root The UI the cursor must be over, whose destruction withdraws the offer
     * @param action What running the command does here
     */
    void registerShortcut(EditorCommand command, Shortcut defaultShortcut, Amethyst::UIObject *root, std::function<void()> action);

    /**
     * @brief Withdraws one offer to run a command, leaving the keys the user chose for it
     * @param command The command that was offered
     * @param root The UI the offer was made against
     */
    void unregisterShortcut(EditorCommand command, Amethyst::UIObject *root);

    /**
     * @brief Puts a command on keys of the user's choosing, in place of the ones it came with
     * @param command The command to rebind
     * @param shortcut The keys to run it with
     */
    void bindShortcut(EditorCommand command, Shortcut shortcut);

    /**
     * @brief Puts a command back on the keys it came with
     * @param command The command to unbind
     */
    void unbindShortcut(EditorCommand command);

    /**
     * @brief The keys that currently run a command
     * @param command The command to look up
     * @return The shortcut, invalid while the command has never been offered
     */
    Shortcut getShortcutForCommand(EditorCommand command) const;

    /**
     * @brief Runs the commands whose shortcuts went down since the last update
     * @param input The keyboard being read
     * @param cursor Where the cursor is, in the space the roots are placed in
     */
    void onUpdate(const Rapture::Input &input, const glm::vec2 &cursor);

  private:
    struct Binding {
        Shortcut shortcut;
        Shortcut defaultShortcut;
        bool wasDown = false;
    };

    struct Handler {
        EditorCommand command = EDITOR_COMMAND_COUNT;
        Amethyst::UIObject *root = nullptr;
        std::function<void()> action;
        Amethyst::EventConnection rootDestroyConn;
    };

    /**
     * @brief Whether a shortcut's key and modifiers are held down and no other modifier is
     * @param input The keyboard being read
     * @param shortcut The shortcut to test
     * @return True while it is held
     */
    static bool isShortcutDown(const Rapture::Input &input, const Shortcut &shortcut);

    /**
     * @brief Runs the handler of a command that the cursor is over
     * @param command The command to run
     * @param cursor Where the cursor is
     */
    void runHandlerUnderCursor(EditorCommand command, const glm::vec2 &cursor);

    std::array<Binding, EDITOR_COMMAND_COUNT> m_bindings;
    std::vector<Handler> m_handlers;
};

#endif // RAPTURE__SHORTCUT_REGISTRY_H
