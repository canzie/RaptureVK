#include "commands/EditorCommands.h"

#include "app/EditorConfig.h"
#include "context/WorkspaceManager.h"
#include "panels/Panel.h"
#include "workspaces/Workspace.h"

#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/Rgba8Image.h>
#include <rapture/platform/RenderWindow.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Rapture::CommandArguments;
using Rapture::CommandParameter;
using Rapture::CommandReply;

static constexpr std::string_view SCREENSHOT_FOLDER_NAME = "rapture-screenshots";

/**
 * @brief The ways a call can name one workspace or one panel, of which at most one may be given
 */
struct TargetSelector {
    std::optional<std::string> label;
    std::optional<uint32_t> id;
    std::optional<std::string> type;

    size_t givenCount() const { return (label ? 1 : 0) + (id ? 1 : 0) + (type ? 1 : 0); }
};

static std::string_view s_labelOf(const Workspace &workspace)
{
    return workspace.tabLabel();
}

static std::string_view s_labelOf(const Panel &panel)
{
    return panel.name();
}

static std::string s_describe(const Workspace &workspace)
{
    return std::to_string(workspace.id()) + "  " + std::string(workspace.tabLabel()) + "  " + std::string(workspace.typeName());
}

static std::string s_describe(const Panel &panel)
{
    const Amethyst::Frame *root = panel.root();
    std::string bounds = root == nullptr ? "not built"
                                         : std::to_string(std::lround(root->absoluteSize.x)) + "x" +
                                               std::to_string(std::lround(root->absoluteSize.y)) + " at " +
                                               std::to_string(std::lround(root->absolutePosition.x)) + "," +
                                               std::to_string(std::lround(root->absolutePosition.y));

    return std::to_string(panel.id()) + "  " + std::string(panel.name()) + "  " + std::string(panel.typeName()) + "  " + bounds +
           (panel.isShown() ? "" : "  hidden behind another tab");
}

/**
 * @brief Reads how a call names its target out of its arguments
 * @param arguments The call's arguments
 * @param labelParameter The parameter holding a label, empty if the call takes none
 * @param idParameter The parameter holding an id
 * @param typeParameter The parameter holding a type name, empty if the call takes none
 * @return The selector
 */
static TargetSelector s_readSelector(const CommandArguments &arguments, std::string_view labelParameter, std::string_view idParameter,
                                     std::string_view typeParameter)
{
    TargetSelector selector;
    if (!labelParameter.empty() && arguments.has(labelParameter) && !arguments.string(labelParameter).empty()) {
        selector.label = std::string(arguments.string(labelParameter));
    }
    if (arguments.has(idParameter)) {
        selector.id = static_cast<uint32_t>(arguments.number(idParameter));
    }
    if (!typeParameter.empty() && arguments.has(typeParameter)) {
        selector.type = std::string(arguments.string(typeParameter));
    }
    return selector;
}

/**
 * @brief Finds the one candidate a selector names, refusing to guess when it names several
 * @param candidates What can be named
 * @param selector How the call named it, exactly one way
 * @param kind What the candidates are, for the error
 * @param error Receives why no single candidate was found, listing the candidates to pick from
 * @return The candidate, or nullptr if the selector names none or several
 */
template <typename T>
static T *s_selectOne(const std::vector<T *> &candidates, const TargetSelector &selector, std::string_view kind, std::string &error)
{
    if (selector.givenCount() != 1) {
        error = "name the " + std::string(kind) + " one way, by its name, --id or --type";
        return nullptr;
    }

    std::vector<T *> matches;
    for (T *candidate : candidates) {
        const bool matched = (selector.label && s_labelOf(*candidate) == *selector.label) ||
                             (selector.id && candidate->id() == *selector.id) ||
                             (selector.type && candidate->typeName() == *selector.type);
        if (matched) {
            matches.push_back(candidate);
        }
    }

    if (matches.size() == 1) {
        return matches.front();
    }

    const std::vector<T *> &listed = matches.empty() ? candidates : matches;
    std::string listing;
    for (T *candidate : listed) {
        listing += "  " + s_describe(*candidate) + "\n";
    }

    error = matches.empty() ? "no " + std::string(kind) + " matches, there are:\n" + listing
                            : std::to_string(matches.size()) + " " + std::string(kind) + "s match, pick one with --id:\n" + listing;
    return nullptr;
}

static std::vector<Workspace *> s_openWorkspaces(const WorkspaceManager &workspaces)
{
    std::vector<Workspace *> open;
    for (const auto &workspace : workspaces.workspaces()) {
        open.push_back(workspace.get());
    }
    return open;
}

static std::vector<Panel *> s_panelsOf(const Workspace &workspace)
{
    std::vector<Panel *> panels;
    for (const auto &panel : workspace.getPanels()) {
        panels.push_back(panel.get());
    }
    return panels;
}

static Workspace *s_findWorkspaceById(const WorkspaceManager &workspaces, uint32_t id)
{
    for (const auto &workspace : workspaces.workspaces()) {
        if (workspace->id() == id) {
            return workspace.get();
        }
    }
    return nullptr;
}

static Panel *s_findPanelById(const Workspace &workspace, uint32_t id)
{
    for (const auto &panel : workspace.getPanels()) {
        if (panel->id() == id) {
            return panel.get();
        }
    }
    return nullptr;
}

/**
 * @brief The workspace a panel command works in, the one its --workspace options name or the one in front
 * @param workspaces The open workspaces
 * @param arguments The call's arguments
 * @param error Receives why there is none
 * @return The workspace, or nullptr
 */
static Workspace *s_workspaceForPanels(const WorkspaceManager &workspaces, const CommandArguments &arguments, std::string &error)
{
    TargetSelector selector = s_readSelector(arguments, "workspace", "workspace-id", {});
    if (selector.givenCount() == 0) {
        if (workspaces.activeWorkspace() == nullptr) {
            error = "no workspace is open";
        }
        return workspaces.activeWorkspace();
    }

    return s_selectOne(s_openWorkspaces(workspaces), selector, "workspace", error);
}

static std::filesystem::path s_screenshotPath(const CommandArguments &arguments, std::string_view subject)
{
    if (arguments.has("out")) {
        return std::filesystem::path(arguments.string("out"));
    }

    std::string stem(subject);
    for (char &c : stem) {
        if (c == ' ' || c == '/') {
            c = '-';
        }
    }

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return std::filesystem::temp_directory_path() / SCREENSHOT_FOLDER_NAME / (stem + "-" + std::to_string(milliseconds) + ".png");
}

/**
 * @brief Writes a capture to disk and answers with where it went
 * @param image The capture
 * @param path Where it is written
 * @param reply Where the answer goes
 */
static void s_answerWithWrittenImage(const Rapture::Rgba8Image &image, const std::filesystem::path &path, CommandReply &reply)
{
    if (image.width == 0 || image.height == 0) {
        reply.fail("nothing was captured");
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    if (!Rapture::Rgba8Image_writePng(image, path)) {
        reply.fail("could not write " + path.string());
        return;
    }

    reply.succeed(path.string() + "  " + std::to_string(image.width) + "x" + std::to_string(image.height));
}

static CommandParameter s_workspaceOption()
{
    return {.name = "workspace",
            .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
            .description = "The label of the workspace to look in, the one in front when no workspace is named"};
}

static CommandParameter s_workspaceIdOption()
{
    return {.name = "workspace-id",
            .type = Rapture::COMMAND_ARGUMENT_TYPE_NUMBER,
            .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
            .description = "The id of the workspace to look in, as workspace list shows it"};
}

static CommandParameter s_outOption()
{
    return {.name = "out",
            .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
            .description = "The PNG file to write, a new file in the temp folder when not given"};
}

EditorCommands::EditorCommands(const EditorConfig &config, WorkspaceManager &workspaces, Rapture::RenderWindow &mainWindow)
    : m_workspaces(&workspaces), m_mainWindow(&mainWindow)
{
    addWorkspaceCommands();
    addPanelCommands();
    addScreenshotCommands();

    if (config.getBool(EDITOR_CONFIG_COMMAND_SOCKET)) {
        m_socketServer = std::make_unique<CommandSocketServer>(m_registry, CommandSocketServer::defaultSocketPath());
    }
}

void EditorCommands::onUpdate()
{
    if (m_socketServer != nullptr) {
        m_socketServer->onUpdate();
    }
}

void EditorCommands::addWorkspaceCommands()
{
    Rapture::CommandGroup &workspace = m_registry.group("workspace", "The open workspaces, the tabs along the top of the window");

    workspace.add({.name = "list",
                   .description = "Lists the open workspaces with their ids and types, the one in front marked with *",
                   .run = [this](const CommandArguments &arguments, CommandReply reply) {
                       (void)arguments;
                       std::string listing;
                       for (const auto &open : m_workspaces->workspaces()) {
                           listing += (open.get() == m_workspaces->activeWorkspace() ? "* " : "  ") + s_describe(*open) + "\n";
                       }
                       reply.succeed(listing);
                   }});

    workspace.add(
        {.name = "select",
         .description = "Brings an open workspace to the front",
         .parameters = {{.name = "label",
                         .kind = Rapture::COMMAND_PARAMETER_KIND_REMAINING,
                         .description = "The workspace's tab label",
                         .complete =
                             [this](const CommandArguments &given) {
                                 (void)given;
                                 std::vector<std::string> labels;
                                 for (const auto &open : m_workspaces->workspaces()) {
                                     labels.emplace_back(open->tabLabel());
                                 }
                                 return labels;
                             }},
                        {.name = "id",
                         .type = Rapture::COMMAND_ARGUMENT_TYPE_NUMBER,
                         .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
                         .description = "The workspace's id, as workspace list shows it"},
                        {.name = "type",
                         .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
                         .description = "The workspace's class, as workspace list shows it"}},
         .run = [this](const CommandArguments &arguments, CommandReply reply) {
             std::string error;
             Workspace *selected =
                 s_selectOne(s_openWorkspaces(*m_workspaces), s_readSelector(arguments, "label", "id", "type"), "workspace", error);
             if (selected == nullptr) {
                 reply.fail(error);
                 return;
             }

             m_workspaces->select(*selected);
             reply.succeed(s_describe(*selected));
         }});
}

void EditorCommands::addPanelCommands()
{
    Rapture::CommandGroup &panel = m_registry.group("panel", "The panels docked in a workspace");

    panel.add({.name = "list",
               .description = "Lists a workspace's panels with their ids, types and where they sit in the window",
               .parameters = {s_workspaceOption(), s_workspaceIdOption()},
               .run = [this](const CommandArguments &arguments, CommandReply reply) {
                   std::string error;
                   Workspace *workspace = s_workspaceForPanels(*m_workspaces, arguments, error);
                   if (workspace == nullptr) {
                       reply.fail(error);
                       return;
                   }

                   std::string listing;
                   for (const auto &docked : workspace->getPanels()) {
                       listing += s_describe(*docked) + "\n";
                   }
                   reply.succeed(listing);
               }});
}

void EditorCommands::addScreenshotCommands()
{
    Rapture::CommandGroup &screenshot = m_registry.group("screenshot", "Writes what the editor shows to PNG files");

    screenshot.add({.name = "window",
                    .description = "Captures the whole main window",
                    .parameters = {s_outOption()},
                    .run = [this](const CommandArguments &arguments, CommandReply reply) {
                        if (m_mainWindow->getWindowContext()->isMinimized()) {
                            reply.fail("the window is minimized, so it draws nothing to capture");
                            return;
                        }

                        // shared because a capture callback has to be copyable and a reply cannot be
                        auto pending = std::make_shared<CommandReply>(std::move(reply));
                        m_mainWindow->captureNextFrame([pending, path = s_screenshotPath(arguments, "window")](const Rapture::Rgba8Image &image) {
                            s_answerWithWrittenImage(image, path, *pending);
                        });
                    }});

    screenshot.add(
        {.name = "panel",
         .description = "Captures one panel, bringing its workspace to the front first",
         .parameters = {{.name = "name",
                         .kind = Rapture::COMMAND_PARAMETER_KIND_REMAINING,
                         .description = "The panel's name, as its tab shows it",
                         .complete =
                             [this](const CommandArguments &given) {
                                 std::string error;
                                 std::vector<std::string> names;
                                 if (Workspace *workspace = s_workspaceForPanels(*m_workspaces, given, error)) {
                                     for (const auto &docked : workspace->getPanels()) {
                                         names.emplace_back(docked->name());
                                     }
                                 }
                                 return names;
                             }},
                        {.name = "id",
                         .type = Rapture::COMMAND_ARGUMENT_TYPE_NUMBER,
                         .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
                         .description = "The panel's id, as panel list shows it"},
                        {.name = "type",
                         .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
                         .description = "The panel's class, as panel list shows it"},
                        s_workspaceOption(),
                        s_workspaceIdOption(),
                        s_outOption()},
         .run = [this](const CommandArguments &arguments, CommandReply reply) {
             if (m_mainWindow->getWindowContext()->isMinimized()) {
                 reply.fail("the window is minimized, so it draws nothing to capture");
                 return;
             }

             std::string error;
             Workspace *workspace = s_workspaceForPanels(*m_workspaces, arguments, error);
             Panel *panel =
                 workspace != nullptr ? s_selectOne(s_panelsOf(*workspace), s_readSelector(arguments, "name", "id", "type"), "panel", error)
                                      : nullptr;
             if (panel == nullptr) {
                 reply.fail(error);
                 return;
             }

             if (workspace != m_workspaces->activeWorkspace()) {
                 m_workspaces->select(*workspace);
             }

             auto pending = std::make_shared<CommandReply>(std::move(reply));
             m_mainWindow->captureNextFrame([this, pending, workspaceId = workspace->id(), panelId = panel->id(),
                                             path = s_screenshotPath(arguments, panel->name())](const Rapture::Rgba8Image &image) {
                 // looked up again, the workspace could have closed before the frame was drawn
                 Workspace *captured = s_findWorkspaceById(*m_workspaces, workspaceId);
                 Panel *shown = captured != nullptr ? s_findPanelById(*captured, panelId) : nullptr;
                 if (shown == nullptr || shown->root() == nullptr) {
                     pending->fail("the panel went away before the frame was drawn");
                     return;
                 }
                 if (!shown->isShown()) {
                     pending->fail(std::string(shown->name()) + " is hidden behind another tab in its dock");
                     return;
                 }

                 const Amethyst::Frame *root = shown->root();
                 Rapture::Rgba8Image cropped =
                     Rapture::Rgba8Image_crop(image, std::lround(root->absolutePosition.x), std::lround(root->absolutePosition.y),
                                              std::lround(root->absoluteSize.x), std::lround(root->absoluteSize.y));
                 s_answerWithWrittenImage(cropped, path, *pending);
             });
         }});
}
