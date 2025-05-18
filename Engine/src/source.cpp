#include "WindowContext/Application.h"
#include "Logging/Log.h"

namespace Rapture {

    class TestApp : public Application {
    public:
        TestApp(int width, int height, const char* title) : Application(width, height, title) {
            RP_INFO("Creating TestApp");
        }

        ~TestApp() {
            RP_INFO("TestApp shutting down...");
        }
        
        
    };

    Application* CreateApplicationWindow(int width, int height, const char* title) {
        return new TestApp(width, height, title);
    }

}

