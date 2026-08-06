#include "EntryPoint.h"
#include "logging/Log.h"
#include "render_targets/swap_chains/SwapChain.h"
#include "utils/EnginePaths.h"
#include "window_context/Application.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif // _WIN32

/**
 * @brief Replaces this process with a fresh editor opening a project
 *
 * Only returns on failure, and never runs destructors, so every shutdown must already have happened.
 *
 * @param executablePath The editor to start
 * @param projectPath The project the new process opens, empty for the default project
 */
static void s_relaunch(const std::filesystem::path &executablePath, const std::filesystem::path &projectPath)
{
    std::string executable = executablePath.string();
    std::string project = projectPath.string();

    const char *args[] = {executable.c_str(), project.empty() ? nullptr : project.c_str(), nullptr};

    RP_INFO("Relaunching the editor");

#if defined(_WIN32)
    _execv(executable.c_str(), const_cast<char *const *>(args));
    RP_ERROR("Relaunch failed: {}", std::strerror(errno));
#else
    execv(executable.c_str(), const_cast<char *const *>(args));
    RP_ERROR("Relaunch failed: {}", std::strerror(errno));
#endif // _WIN32
}

// The main entry point of the application
int main(int argc, char **argv)
{
    Rapture::Log::Init();
    Rapture::SwapChain::renderMode = Rapture::RenderMode::OFFSCREEN;

    auto *app = Rapture::CreateApplicationWindow(1920, 1080, "Rapture Editor", argc, argv);

    if (app) {
        // Simple log without format string
        RP_INFO("Starting application");

        // Run the application
        app->run();

        bool relaunch = app->isRelaunchRequested();
        std::filesystem::path relaunchProject = app->getRelaunchProject();
        std::filesystem::path executable = Rapture::EnginePaths::executable();

        // Cleanup
        delete app;

        // after the teardown above, because exec discards the image without unwinding
        if (relaunch) {
            s_relaunch(executable, relaunchProject);
        }
    }

    return 0;
}
