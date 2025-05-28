#include "WindowContext/Application.h"
#include "Logging/Log.h"

namespace Rapture {

class TestApp : public Application {
    public:
        TestApp(int width, int height, const char* title) : Application(width, height, title) {

            // Log startup message
            RP_INFO("Rapture Editor starting up...");
            
            // Initialize event listeners
            setupEventHandlers();
            
            // Push main editor layer
            //pushLayer(new TestLayer());
            
            // Push ImGui layer as an overlay so it renders on top
            //pushOverlay(new ImGuiLayer());

        }

        ~TestApp() {
            RP_INFO("TestApp shutting down...");
        }

    private:
        void setupEventHandlers() {

        }
    
    // Event listener IDs for cleanup
    size_t m_sceneActivatedListenerId = 0;
    size_t m_worldActivatedListenerId = 0;
    size_t m_worldTransitionListenerId = 0;
        
    };

    //Application* CreateApplicationWindow(int width, int height, const char* title) {
     //   return new TestApp(width, height, title);
    //}

}

