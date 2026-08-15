#include "ShortcutRegistry.h"

#include <logging/Log.h>
#include <utils/rp_assert.h>

static bool s_isModifierDown(const Rapture::Input &input, Rapture::KeyCode left, Rapture::KeyCode right)
{
    return input.isKeyPressed(left) || input.isKeyPressed(right);
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
