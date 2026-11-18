#include "WindowManager.h"

#include "panels/ImportPanel.h"
#include "ui/FileBrowser.h"

#include <amethyst__vk13_glfw.h>
#include <rapture/app/Application.h>
#include <rapture/gpu/swap_chains/SwapChain.h>

static constexpr int32_t FILE_DIALOG_WINDOW_WIDTH = 880;
static constexpr int32_t FILE_DIALOG_WINDOW_HEIGHT = 560;

WindowManager::WindowManager(Amethyst::AmVulkanBackend &backend, Amethyst::Window &mainWindow, WindowDrawer drawWindow)
    : m_backend(&backend), m_mainWindow(&mainWindow), m_drawWindow(std::move(drawWindow))
{
}

WindowManager::~WindowManager()
{
    for (auto &open : m_windows) {
        m_backend->unregisterWindow(open->renderWindow->getWindowContext()->getNativeWindowContext());
        open->renderWindow->onFrame = nullptr;
        open->renderWindow->onClose = nullptr;
    }
    m_windows.clear();
}

void WindowManager::openWindow(int32_t width, int32_t height, std::string_view title, WindowContentBuilder build)
{
    Rapture::RenderWindow &renderWindow =
        Rapture::Application::getInstance().createSecondaryWindow(width, height, std::string(title).c_str());
    auto swapChain = renderWindow.getSwapChain();

    auto open = std::make_unique<OpenWindow>();
    open->renderWindow = &renderWindow;
    open->window.setFramebufferSize(
        {static_cast<float>(swapChain->getExtent().width), static_cast<float>(swapChain->getExtent().height)});
    open->window.absoluteRotation = 0.0f;

    m_backend->registerWindow(renderWindow.getWindowContext()->getNativeWindowContext(), &open->window);

    std::function<void()> close = [&renderWindow]() { renderWindow.getWindowContext()->requestClose(); };
    open->content = build(open->window, close);

    OpenWindow *opened = open.get();
    m_windows.push_back(std::move(open));

    opened->swapchainRecreatedConn = swapChain->onRecreated.connect([this, opened]() { resizeWindow(*opened); });
    renderWindow.onFrame = [this, opened](Rapture::RenderWindow &window) { m_drawWindow(opened->window, window); };
    renderWindow.onClose = [this, opened]() { closeWindow(opened); };
}

void WindowManager::openFileDialog(FileDialogMode mode, std::function<void(const std::filesystem::path &)> onConfirm)
{
    auto confirm = [this, onConfirm = std::move(onConfirm)](const std::filesystem::path &path) {
        m_lastDialogDirectory = path.parent_path();
        if (onConfirm) {
            onConfirm(path);
        }
    };

    openWindow(FILE_DIALOG_WINDOW_WIDTH, FILE_DIALOG_WINDOW_HEIGHT, "Open File",
               [mode, startDirectory = m_lastDialogDirectory, confirm = std::move(confirm)](Amethyst::Window &window,
                                                                                            const std::function<void()> &close) {
                   auto fileBrowser = std::make_shared<FileBrowser>(window, mode, startDirectory);
                   fileBrowser->onConfirm = confirm;
                   fileBrowser->onClose = close;
                   return fileBrowser;
               });
}

void WindowManager::openImportPanel(const std::filesystem::path &source, const std::filesystem::path &outputFolder)
{
    struct Session {
        std::unique_ptr<ImportPanel> panel;
        Amethyst::TickHandle watchTick;
        bool pendingDestroy = false;
    };
    auto session = std::make_shared<Session>();
    session->panel = std::make_unique<ImportPanel>(*m_mainWindow, source, outputFolder);
    session->panel->onClose = [session](void) { session->pendingDestroy = true; };
    session->watchTick = m_mainWindow->registerTick([session](float) {
        if (session->pendingDestroy) {
            session->panel.reset();
            session->watchTick.unregister();
        }
    });
}

void WindowManager::resizeWindow(OpenWindow &open)
{
    Rapture::Application::getInstance().getVulkanContext().waitIdle();

    const VkExtent2D extent = open.renderWindow->getSwapChain()->getExtent();
    open.window.setFramebufferSize({static_cast<float>(extent.width), static_cast<float>(extent.height)});
}

void WindowManager::closeWindow(OpenWindow *window)
{
    m_backend->unregisterWindow(window->renderWindow->getWindowContext()->getNativeWindowContext());
    std::erase_if(m_windows, [window](const std::unique_ptr<OpenWindow> &open) { return open.get() == window; });
}
