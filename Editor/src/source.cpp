#include "WindowContext/Application.h"
#include "Logging/Log.h"
#include "Layers/TestLayer.h"
#include "Layers/ImGuiLayer.h"

#include "EntryPoint.h"
#include "Scenes/SceneManager.h"
#include "Events/Events.h"
#include "Scenes/Project.h"

// The main Editor application class
class EditorApp : public Rapture::Application {
public:
    EditorApp(int width, int height, const char* title) : Application(width, height, title) {
        
        // Log startup message
        Rapture::RP_INFO("Rapture Editor starting up...");
        
        // Initialize event listeners
        setupEventHandlers();
        
        // Push main editor layer
        pushLayer(new TestLayer());
        
        // Push ImGui layer as an overlay so it renders on top
        //pushOverlay(new ImGuiLayer());
    }
    
    ~EditorApp() {
        Rapture::RP_INFO("Rapture Editor shutting down...");
        
        // Clean up event listeners
        Rapture::GameEvents::onSceneActivated().removeListener(m_sceneActivatedListenerId);
        Rapture::GameEvents::onWorldActivated().removeListener(m_worldActivatedListenerId);
        Rapture::GameEvents::onWorldTransitionRequested().removeListener(m_worldTransitionListenerId);
    }
    
private:
    void setupEventHandlers() {
        // Scene change events
        m_sceneActivatedListenerId = Rapture::GameEvents::onSceneActivated().addListener([](std::shared_ptr<Rapture::Scene> scene) {
            Rapture::RP_INFO("Scene activated");
        });
        
        m_worldActivatedListenerId = Rapture::GameEvents::onWorldActivated().addListener([](std::shared_ptr<Rapture::World> world) {
            Rapture::RP_INFO("World activated: {0}", world->getName());
        });
        
        // World transition events
        m_worldTransitionListenerId = Rapture::GameEvents::onWorldTransitionRequested().addListener([this](const std::string& worldName) {
            Rapture::RP_INFO("World transition requested: {0}", worldName);
            //transitionToWorld(worldName);
        });
    }
    
    // Event listener IDs for cleanup
    size_t m_sceneActivatedListenerId = 0;
    size_t m_worldActivatedListenerId = 0;
    size_t m_worldTransitionListenerId = 0;
};

// Implementation of the function declared in AppEntryPoint.h
Rapture::Application* Rapture::CreateApplicationWindow(int width, int height, const char* title) {
    return new EditorApp(width, height, title);
}