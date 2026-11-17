# Editor Commands

The editor can be driven with text commands. A command is words: the groups leading to it, its name, then its arguments, for example `screenshot panel Outliner --workspace "World Editor"`. `help` lists the top level, `help screenshot panel` describes one command with its parameters.

## Registry

`CommandRegistry` (`Engine/include/rapture/core/commands/CommandRegistry.h`) holds a tree of groups and commands and knows nothing about where a line came from, so the socket, a future in-editor terminal and a game's dev console all feed it the same way.

- A group holds further groups and commands under one sorted map of names, so a name is either a group or a command.
- A command declares its parameters: positional words in order, `--name value` options (a bool option is a bare `--name`), and at most one parameter taking every remaining word. Types are string, number, bool and choice, a fixed set of words.
- Every group, command and parameter carries a description, registration refuses one without.
- Arguments reach the handler checked and converted, read by parameter name. The handler answers through a `CommandReply`, which it may hold past its own return. A reply never answered fails, so whoever issued the call is never left waiting.
- `complete` lists what the last word of a partly typed line can become: a group's children, a command's unused options, a choice's words, or what a parameter's completer returns. A completer sees the arguments already typed, so the panel names it offers follow `--workspace`.

## Socket

`CommandSocketServer` (`Editor/src/commands/`) is a Unix socket at `$XDG_RUNTIME_DIR/rapture-<pid>.sock`, connectable only by the current user and run once per frame on the main thread. A client writes one command per line and reads back `ok <bytes>` or `error <bytes>` followed by the text. The `commandSocket` entry in the editor config turns it off entirely.

`tools/editor/cli.py workspace list` issues a command from a shell, `--complete "<line>"` asks for completions.

## Editor commands

`EditorCommands` (`Editor/src/commands/`) registers them. Calls run before the main window's frame is drawn, so a screenshot shows what the calls before it changed.

- `workspace list`, `workspace select <label> | --id | --type`
- `panel list [--workspace | --workspace-id]`
- `screenshot window [--out]`, `screenshot panel <name> | --id | --type [--workspace | --workspace-id] [--out]`, which block until the PNG is written

Workspaces and panels are named by label, by id or by class. Ids are handed out as workspaces open and panels are docked, unique for the run, and shown by the list commands. A name that matches several is an error listing them, never a guess.

Related: [[Lua Scripting]], [[Engine Hardening]]
