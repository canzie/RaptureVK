#include "layers/EditorLayer.h"
#include "layers/AmethystLayer.h"
#include "layers/TestLayer.h"
#include "core/utils/Log.h"
#include "app/Application.h"

#include "EntryPoint.h"
#include "core/events/Events.h"
#include "core/events/GameEvents.h"
#include "LauncherConfig.h"
#include "scene/Project.h"

#include <filesystem>
#include <memory>

// The main Editor application class
class EditorApp : public Rapture::Application {
  public:
    EditorApp(int width, int height, const char *title, int argc, char **argv) : Application(width, height, title)
    {

        // Log startup message
        RP_INFO("Rapture Editor starting up...");

        // Initialize event listeners
        setupEventHandlers();

        std::filesystem::path projectPath =
            argc > 1 ? std::filesystem::path(argv[1]) : LauncherConfig::load().autoLaunchProject();
        if (!projectPath.empty()) {
            openProject(projectPath);
        }

        pushLayer(std::make_unique<TestLayer>());

        // Without a project there is no scene to view, so the UI layer shows the launcher instead
        if (hasProject()) {
            // Push editor view layer (camera, controller, input)
            pushLayer(std::make_unique<EditorLayer>());
        }

        // Push Amethyst UI layer as an overlay so it renders on top

        pushOverlay(std::make_unique<AmethystLayer>());
    }

    ~EditorApp()
    {
        RP_INFO("Rapture Editor shutting down...");

        // Clean up event listeners
        Rapture::GameEvents::onSceneActivated().removeListener(m_sceneActivatedListenerId);
        Rapture::GameEvents::onWorldActivated().removeListener(m_worldActivatedListenerId);
        Rapture::GameEvents::onWorldTransitionRequested().removeListener(m_worldTransitionListenerId);
    }

  private:
    void setupEventHandlers()
    {
        // Scene change events
        m_sceneActivatedListenerId = Rapture::GameEvents::onSceneActivated().addListener(
            [](Rapture::Scene& scene) { RP_INFO("Scene activated"); });

        m_worldActivatedListenerId = Rapture::GameEvents::onWorldActivated().addListener(
            [](Rapture::World *world) { RP_INFO("World activated: {0}", world->getName()); });

        // World transition events
        m_worldTransitionListenerId =
            Rapture::GameEvents::onWorldTransitionRequested().addListener([this](const std::string &worldName) {
                RP_INFO("World transition requested: {0}", worldName);
                // transitionToWorld(worldName);
            });
    }

    // Event listener IDs for cleanup
    size_t m_sceneActivatedListenerId = 0;
    size_t m_worldActivatedListenerId = 0;
    size_t m_worldTransitionListenerId = 0;
};

// Implementation of the function declared in AppEntryPoint.h
Rapture::Application *Rapture::CreateApplicationWindow(int width, int height, const char *title, int argc, char **argv)
{
    return new EditorApp(width, height, title, argc, argv);
}