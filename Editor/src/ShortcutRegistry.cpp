#include "ShortcutRegistry.h"

#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/rp_assert.h>

#include <string>

static bool s_isModifierDown(const Rapture::Input &input, Rapture::KeyCode left, Rapture::KeyCode right)
{
    return input.isKeyPressed(left) || input.isKeyPressed(right);
}

static std::string s_keyName(Rapture::KeyCode key)
{
    const int code = static_cast<int>(key);
    if (code >= Rapture::KEY_F1 && code <= Rapture::KEY_F12) {
        return "F" + std::to_string(code - Rapture::KEY_F1 + 1);
    }

    switch (key) {
    case Rapture::KEY_SPACE:
        return "Space";
    case Rapture::KEY_ESCAPE:
        return "Esc";
    case Rapture::KEY_ENTER:
        return "Enter";
    case Rapture::KEY_TAB:
        return "Tab";
    case Rapture::KEY_BACKSPACE:
        return "Backspace";
    case Rapture::KEY_INSERT:
        return "Insert";
    case Rapture::KEY_DELETE:
        return "Delete";
    case Rapture::KEY_RIGHT:
        return "Right";
    case Rapture::KEY_LEFT:
        return "Left";
    case Rapture::KEY_DOWN:
        return "Down";
    case Rapture::KEY_UP:
        return "Up";
    case Rapture::KEY_PAGE_UP:
        return "Page Up";
    case Rapture::KEY_PAGE_DOWN:
        return "Page Down";
    case Rapture::KEY_HOME:
        return "Home";
    case Rapture::KEY_END:
        return "End";
    default:
        break;
    }

    // printable keys carry the code of the character on them
    if (code > 32 && code < 127) {
        return std::string(1, static_cast<char>(code));
    }
    return "Key " + std::to_string(code);
}

std::string Shortcut_toString(const Shortcut &shortcut)
{
    if (!shortcut.isValid()) {
        return {};
    }

    std::string text;
    if ((shortcut.modifiers & SHORTCUT_MOD_CONTROL) != 0) {
        text += "Ctrl+";
    }
    if ((shortcut.modifiers & SHORTCUT_MOD_SHIFT) != 0) {
        text += "Shift+";
    }
    if ((shortcut.modifiers & SHORTCUT_MOD_ALT) != 0) {
        text += "Alt+";
    }
    text += s_keyName(shortcut.key);
    return text;
}

bool ShortcutRegistry::isShortcutDown(const Rapture::Input &input, const Shortcut &shortcut)
{
    if (!shortcut.isValid() || !input.isKeyPressed(shortcut.key)) {
        return false;
    }

    const bool control = s_isModifierDown(input, Rapture::KEY_LEFT_CONTROL, Rapture::KEY_RIGHT_CONTROL);
    const bool shift = s_isModifierDown(input, Rapture::KEY_LEFT_SHIFT, Rapture::KEY_RIGHT_SHIFT);
    const bool alt = s_isModifierDown(input, Rapture::KEY_LEFT_ALT, Rapture::KEY_RIGHT_ALT);

    // a modifier the shortcut does not ask for must be up, so Ctrl+Shift+P never runs Ctrl+P
    return control == ((shortcut.modifiers & SHORTCUT_MOD_CONTROL) != 0) &&
           shift == ((shortcut.modifiers & SHORTCUT_MOD_SHIFT) != 0) && alt == ((shortcut.modifiers & SHORTCUT_MOD_ALT) != 0);
}

void ShortcutRegistry::registerShortcut(EditorCommand command, Shortcut defaultShortcut, Amethyst::UIObject *root,
                                        std::function<void()> action)
{
    RP_ASSERT(root != nullptr && action != nullptr, "a shortcut runs a command from somewhere, so it needs both");

    Binding &binding = m_bindings[command];
    binding.defaultShortcut = defaultShortcut;
    if (!binding.shortcut.isValid()) {
        binding.shortcut = defaultShortcut;
    }

    Handler &handler = m_handlers.emplace_back();
    handler.command = command;
    handler.root = root;
    handler.action = std::move(action);
    handler.rootDestroyConn =
        root->onDestroy.connect([this, command, root](Amethyst::Instance *) { unregisterShortcut(command, root); });
}

void ShortcutRegistry::unregisterShortcut(EditorCommand command, Amethyst::UIObject *root)
{
    std::erase_if(m_handlers,
                  [command, root](const Handler &handler) { return handler.command == command && handler.root == root; });
}

void ShortcutRegistry::bindShortcut(EditorCommand command, Shortcut shortcut)
{
    m_bindings[command].shortcut = shortcut;
}

void ShortcutRegistry::unbindShortcut(EditorCommand command)
{
    m_bindings[command].shortcut = m_bindings[command].defaultShortcut;
}

Shortcut ShortcutRegistry::getShortcutForCommand(EditorCommand command) const
{
    return m_bindings[command].shortcut;
}

void ShortcutRegistry::runHandlerUnderCursor(EditorCommand command, const glm::vec2 &cursor)
{
    const Amethyst::vec2 point(cursor.x, cursor.y);

    for (const Handler &handler : m_handlers) {
        if (handler.command != command || !handler.root->isHitTestVisible() || !handler.root->containsPoint(point)) {
            continue;
        }

        handler.action();
        return;
    }
}

void ShortcutRegistry::onUpdate(const Rapture::Input &input, const glm::vec2 &cursor)
{
    for (uint32_t command = 0; command < EDITOR_COMMAND_COUNT; ++command) {
        Binding &binding = m_bindings[command];

        const bool isDown = isShortcutDown(input, binding.shortcut);
        const bool wasPressed = isDown && !binding.wasDown;
        binding.wasDown = isDown;

        if (wasPressed) {
            runHandlerUnderCursor(static_cast<EditorCommand>(command), cursor);
        }
    }
}
