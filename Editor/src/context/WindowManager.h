#ifndef RAPTURE__WINDOW_MANAGER_H
#define RAPTURE__WINDOW_MANAGER_H

#include <amethyst/Amethyst.h>
#include <core/events/EventSignal.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace Amethyst {
class AmVulkanBackend;
} // namespace Amethyst

namespace Rapture {
class RenderWindow;
} // namespace Rapture

/**
 * @brief Whether a file dialog opens an existing file or names a new one
 */
enum FileDialogMode {
    FILE_DIALOG_OPEN,
    FILE_DIALOG_SAVE,
    FILE_DIALOG_COUNT
};

/**
 * @brief Opens and owns the editor's windows besides the main one, and the dialogs that live outside any workspace
 */
class WindowManager {
  public:
    /**
     * @brief Fills a window, returning whatever has to stay alive for as long as the window is open
     */
    using WindowContentBuilder = std::function<std::shared_ptr<void>(Amethyst::Window &, const std::function<void()> &close)>;

    /**
     * @brief Draws a UI window into the render window it belongs to
     */
    using WindowDrawer = std::function<void(Amethyst::Window &, Rapture::RenderWindow &)>;

    /**
     * @brief Creates a manager with no windows open
     * @param backend The UI backend windows are registered with for input
     * @param mainWindow The main window, which panels opened over the editor are built into
     * @param drawWindow Draws a window each frame
     */
    WindowManager(Amethyst::AmVulkanBackend &backend, Amethyst::Window &mainWindow, WindowDrawer drawWindow);
    ~WindowManager();

    WindowManager(const WindowManager &) = delete;
    WindowManager &operator=(const WindowManager &) = delete;

    /**
     * @brief Opens a window of its own, filled by a builder
     * @param width Width of the window in pixels
     * @param height Height of the window in pixels
     * @param title Text shown in the window's title bar
     * @param build Fills the window, called once before it is first drawn
     */
    void openWindow(int32_t width, int32_t height, std::string_view title, WindowContentBuilder build);

    /**
     * @brief Opens the dialog a file is chosen through
     * @param mode Whether the dialog opens an existing file or names a new one
     * @param onConfirm Called with the chosen path, not called if the dialog is dismissed
     */
    void openFileDialog(FileDialogMode mode, std::function<void(const std::filesystem::path &)> onConfirm);

    /**
     * @brief Opens the panel a source file is imported through, over the main window
     * @param source The file to import
     * @param outputFolder The folder the imported assets are written to
     */
    void openImportPanel(const std::filesystem::path &source, const std::filesystem::path &outputFolder);

  private:
    struct OpenWindow {
        Rapture::RenderWindow *renderWindow = nullptr;
        Amethyst::Window window;
        Rapture::EventConnection swapchainRecreatedConn;
        // Declared last so it is destroyed while the Window it built into is still alive.
        std::shared_ptr<void> content;
    };

    /**
     * @brief Sizes a window's UI to its swapchain after the swapchain was recreated
     * @param window The window whose swapchain changed
     */
    void resizeWindow(OpenWindow &window);

    /**
     * @brief Forgets a window the user closed
     * @param window The window that closed
     */
    void closeWindow(OpenWindow *window);

  private:
    Amethyst::AmVulkanBackend *m_backend = nullptr;
    Amethyst::Window *m_mainWindow = nullptr;
    WindowDrawer m_drawWindow;

    std::vector<std::unique_ptr<OpenWindow>> m_windows;
    std::filesystem::path m_lastDialogDirectory;
};

#endif // RAPTURE__WINDOW_MANAGER_H
