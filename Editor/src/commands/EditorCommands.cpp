#include "commands/EditorCommands.h"

#include "context/WorkspaceManager.h"
#include "panels/Panel.h"
#include "workspaces/Workspace.h"

#include <rapture/core/utils/Rgba8Image.h>
#include <rapture/platform/RenderWindow.h>

#include <cmath>
#include <memory>
#include <string>

static Workspace *s_findWorkspaceByLabel(const WorkspaceManager &workspaces, std::string_view label)
{
    for (const auto &workspace : workspaces.workspaces()) {
        if (workspace->tabLabel() == label) {
            return workspace.get();
        }
    }
    return nullptr;
}

static Panel *s_findPanelInActiveWorkspace(const WorkspaceManager &workspaces, std::string_view name)
{
    Workspace *active = workspaces.activeWorkspace();
    if (active == nullptr) {
        return nullptr;
    }

    for (const auto &panel : active->getPanels()) {
        if (panel->name() == name) {
            return panel.get();
        }
    }
    return nullptr;
}

static std::string s_describePanelBounds(const Panel &panel)
{
    const Amethyst::Frame *root = panel.root();
    return std::to_string(static_cast<int>(root->absoluteSize.x)) + "x" + std::to_string(static_cast<int>(root->absoluteSize.y)) +
           " at " + std::to_string(static_cast<int>(root->absolutePosition.x)) + "," +
           std::to_string(static_cast<int>(root->absolutePosition.y));
}

/**
 * @brief Writes a capture to disk and answers with where it went
 * @param image The capture
 * @param path Where it is written
 * @param reply Where the answer goes
 */
static void s_answerWithWrittenImage(const Rapture::Rgba8Image &image, const std::string &path, Rapture::CommandReply &reply)
{
    if (image.width == 0 || image.height == 0) {
        reply.fail("nothing was captured");
        return;
    }

    if (!Rapture::Rgba8Image_writePng(image, path)) {
        reply.fail("could not write " + path);
        return;
    }

    reply.succeed(path + " " + std::to_string(image.width) + "x" + std::to_string(image.height));
}

EditorCommands::EditorCommands(WorkspaceManager &workspaces, Rapture::RenderWindow &mainWindow)
    : m_workspaces(&workspaces), m_mainWindow(&mainWindow),
      m_socketServer(m_registry, Rapture::CommandSocketServer::defaultSocketPath())
{
    addWorkspaceCommands();
    addPanelCommands();
    addScreenshotCommands();
}

void EditorCommands::poll()
{
    m_socketServer.poll();
}

void EditorCommands::addWorkspaceCommands()
{
    m_registry.add("workspace.list", "Lists the open workspaces, the one in front marked with *", {},
                   [this](const Rapture::CommandArguments &arguments, Rapture::CommandReply reply) {
                       (void)arguments;
                       std::string listing;
                       for (const auto &workspace : m_workspaces->workspaces()) {
                           listing += workspace.get() == m_workspaces->activeWorkspace() ? "* " : "  ";
                           listing += std::string(workspace->tabLabel()) + "\n";
                       }
                       reply.succeed(listing);
                   });

    m_registry.add("workspace.select", "Brings an open workspace to the front", {{"label", Rapture::COMMAND_ARGUMENT_STRING}},
                   [this](const Rapture::CommandArguments &arguments, Rapture::CommandReply reply) {
                       Workspace *workspace = s_findWorkspaceByLabel(*m_workspaces, arguments.string(0));
                       if (workspace == nullptr) {
                           reply.fail("no open workspace is labelled " + std::string(arguments.string(0)));
                           return;
                       }

                       m_workspaces->select(*workspace);
                       reply.succeed();
                   });
}

void EditorCommands::addPanelCommands()
{
    m_registry.add("panel.list", "Lists the panels of the workspace in front, with their size and position in the window", {},
                   [this](const Rapture::CommandArguments &arguments, Rapture::CommandReply reply) {
                       (void)arguments;
                       Workspace *active = m_workspaces->activeWorkspace();
                       if (active == nullptr) {
                           reply.fail("no workspace is open");
                           return;
                       }

                       std::string listing;
                       for (const auto &panel : active->getPanels()) {
                           listing += std::string(panel->name()) + "  " + s_describePanelBounds(*panel) + "\n";
                       }
                       reply.succeed(listing);
                   });
}

void EditorCommands::addScreenshotCommands()
{
    m_registry.add("screenshot.window", "Writes what the main window shows to a PNG file", {{"path", Rapture::COMMAND_ARGUMENT_STRING}},
                   [this](const Rapture::CommandArguments &arguments, Rapture::CommandReply reply) {
                       if (m_mainWindow->getWindowContext()->isMinimized()) {
                           reply.fail("the window is minimized, so it draws nothing to capture");
                           return;
                       }

                       // shared because a capture callback has to be copyable and a reply cannot be
                       auto pending = std::make_shared<Rapture::CommandReply>(std::move(reply));
                       m_mainWindow->captureNextFrame([pending, path = std::string(arguments.string(0))](const Rapture::Rgba8Image &image) {
                           s_answerWithWrittenImage(image, path, *pending);
                       });
                   });

    m_registry.add("screenshot.panel", "Writes what one panel of the workspace in front shows to a PNG file",
                   {{"name", Rapture::COMMAND_ARGUMENT_STRING}, {"path", Rapture::COMMAND_ARGUMENT_STRING}},
                   [this](const Rapture::CommandArguments &arguments, Rapture::CommandReply reply) {
                       if (m_mainWindow->getWindowContext()->isMinimized()) {
                           reply.fail("the window is minimized, so it draws nothing to capture");
                           return;
                       }

                       std::string name(arguments.string(0));
                       if (s_findPanelInActiveWorkspace(*m_workspaces, name) == nullptr) {
                           reply.fail("the workspace in front has no panel named " + name);
                           return;
                       }

                       auto pending = std::make_shared<Rapture::CommandReply>(std::move(reply));
                       m_mainWindow->captureNextFrame(
                           [this, pending, name, path = std::string(arguments.string(1))](const Rapture::Rgba8Image &image) {
                               // looked up again, the frame being captured is the one the panel was last laid out for
                               Panel *panel = s_findPanelInActiveWorkspace(*m_workspaces, name);
                               if (panel == nullptr) {
                                   pending->fail("the panel " + name + " went away before the frame was drawn");
                                   return;
                               }

                               const Amethyst::Frame *root = panel->root();
                               Rapture::Rgba8Image cropped = Rapture::Rgba8Image_crop(
                                   image, std::lround(root->absolutePosition.x), std::lround(root->absolutePosition.y),
                                   std::lround(root->absoluteSize.x), std::lround(root->absoluteSize.y));
                               s_answerWithWrittenImage(cropped, path, *pending);
                           });
                   });
}
