#ifndef RAPTURE__EDITOR_COMMANDS_H
#define RAPTURE__EDITOR_COMMANDS_H

#include "commands/CommandSocketServer.h"

#include <rapture/core/commands/CommandRegistry.h>

#include <memory>

class EditorConfig;
class WorkspaceManager;

namespace Rapture {
class RenderWindow;
}

/**
 * @brief The commands the editor can be driven with as text, and the socket other processes issue them over
 */
class EditorCommands {
  public:
    /**
     * @brief Registers the editor's commands, and opens the socket unless the config turns it off
     * @param config The editor's settings
     * @param workspaces The open workspaces, which have to outlive this
     * @param mainWindow The editor's main window, which has to outlive this
     */
    EditorCommands(const EditorConfig &config, WorkspaceManager &workspaces, Rapture::RenderWindow &mainWindow);

    EditorCommands(const EditorCommands &) = delete;
    EditorCommands &operator=(const EditorCommands &) = delete;

    /**
     * @brief Runs every call that has arrived over the socket
     */
    void onUpdate();

    const Rapture::CommandRegistry &registry() const { return m_registry; }

  private:
    void addWorkspaceCommands();
    void addPanelCommands();
    void addScreenshotCommands();

  private:
    WorkspaceManager *m_workspaces;
    Rapture::RenderWindow *m_mainWindow;
    Rapture::CommandRegistry m_registry;
    std::unique_ptr<CommandSocketServer> m_socketServer;
};

#endif // RAPTURE__EDITOR_COMMANDS_H
