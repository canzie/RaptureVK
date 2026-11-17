# Editor Commands

The editor can be driven with text command calls such as `workspace.select("World Editor")` or `screenshot.panel("Outliner", "/tmp/outliner.png")`. `help()` lists every command with its parameters.

`CommandRegistry` (`Engine/include/rapture/core/commands/CommandRegistry.h`) is the core: it reads a call out of a line of text, checks its arguments against what the command declares (string, number, bool), and runs it. A command answers through a `CommandReply`, which it may hold past its own return, for example until a frame has been captured. A reply that is never answered fails, so whoever issued the call is never left waiting. Nothing in the registry knows where the text came from, so an in-editor terminal feeds it the same way the socket does.

`CommandSocketServer` (`Engine/include/rapture/platform/CommandSocketServer.h`) is one transport: a Unix socket at `$XDG_RUNTIME_DIR/rapture-<pid>.sock`, connectable only by the current user, polled by the main thread once per frame. A client writes one call per line and reads back `ok <bytes>` or `error <bytes>` followed by the text.

`EditorCommands` (`Editor/src/commands/`) registers the editor's commands and owns the socket. Calls run before the main window's frame is drawn, so a screenshot captures what the calls before it changed.

`tools/editor/cli.py 'workspace.list()'` issues calls from a shell.

## Commands

- `workspace.list()`, `workspace.select(label)`
- `panel.list()` for the workspace in front, with each panel's size and position
- `screenshot.window(path)`, `screenshot.panel(name, path)`, which block until the PNG is written

Related: [[Lua Scripting]], [[Engine Hardening]]
