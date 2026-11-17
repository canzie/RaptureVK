#ifndef RAPTURE__BOTTOM_BAR_H
#define RAPTURE__BOTTOM_BAR_H

#include "context/EditorContext.h"
#include "workspaces/Workspace.h"
#include <amethyst/Amethyst.h>

#include <rapture/core/commands/CommandRegistry.h>

#include <memory>
#include <string>

class ContentBrowserPanel;

class BottomBar {
  public:
    BottomBar(Amethyst::Window *window, const EditorContext &context);
    ~BottomBar();
    BottomBar(const BottomBar &) = delete;
    BottomBar &operator=(const BottomBar &) = delete;
    BottomBar(BottomBar &&) = delete;
    BottomBar &operator=(BottomBar &&) = delete;

    void setCurrentWorkspace(Workspace *workspace);

  private:
    void setupCommandLine(Amethyst::UIScope &root);

    /**
     * @brief Runs what the command line holds and clears it
     */
    void runCommandLine();

    /**
     * @brief Completes the word the command line ends in, as far as every candidate agrees
     */
    void completeCommandLine();

    /**
     * @brief Shows the first line of a command's answer beside the command line and logs all of it
     * @param result How the command ended
     * @param text The answer
     */
    void showCommandAnswer(Rapture::CommandResult result, const std::string &text);

    void setupContentBrowserToggle(void);
    void toggleContentBrowser(void);

    /**
     * @brief Moves the floating content browser into the current workspace's layout
     */
    void dockContentBrowser(void);

  private:
    EditorContext m_editor;
    Workspace *m_currWorkspace = nullptr;
    Amethyst::Window *m_window = nullptr;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextButton *m_contentBrowserBtn = nullptr;
    Amethyst::TextInput *m_commandInput = nullptr;
    Amethyst::TextLabel *m_commandAnswer = nullptr;
    Amethyst::Popup *m_contentBrowserPopup = nullptr;
    std::unique_ptr<ContentBrowserPanel> m_contentBrowserPanel;
};

#endif // RAPTURE__BOTTOM_BAR_H
