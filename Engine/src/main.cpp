#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include <cstdio>

int main() {
    Rapture::Log::Init();

    auto* app = Rapture::CreateApplicationWindow(800, 600, "Rapture Engine");

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