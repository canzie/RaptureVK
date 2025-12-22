#include "EntryPoint.h"
#include "Logging/Log.h"
#include "RenderTargets/SwapChains/SwapChain.h"
#include "WindowContext/Application.h"

// The main entry point of the application
int main(int argc, char **argv)
{
    Rapture::Log::Init();
    Rapture::SwapChain::renderMode = Rapture::RenderMode::OFFSCREEN;

    auto *app = Rapture::CreateApplicationWindow(1920, 1080, "Rapture Editor");

    if (app) {
        // Simple log without format string
        Rapture::RP_INFO("Starting application");

        // Run the application
        app->run();

        // Cleanup
        delete app;
    }

    return 0;
}
