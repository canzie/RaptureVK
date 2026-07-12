#include "Project.h"

#include "events/ProjectEvents.h"
#include "logging/Log.h"

namespace Rapture {

Project::Project()
    : m_config{"New Project", std::filesystem::current_path(), std::filesystem::current_path(), "DefaultWorld"}
{
    RP_CORE_INFO("Creating Project: {0}", m_config.name);

    World *defaultWorld = m_sceneManager.createWorld("DefaultWorld");
    auto defaultScene = m_sceneManager.createScene("DefaultScene");
    m_sceneManager.setActiveScene("DefaultScene");

    defaultWorld->addScene("DefaultScene", defaultScene);
    defaultWorld->setMainScene("DefaultScene");

    m_sceneManager.setActiveWorld("DefaultWorld");
}

void Project::saveProject(std::filesystem::path path)
{
    (void)path;

    SerialDocument doc;
    WriteNode root = doc.root();
    ProjectEvents::onProjectSerialize().publish(root);
}

std::unique_ptr<Project> Project::loadProject(std::filesystem::path path)
{
    (void)path;

    auto project = std::make_unique<Project>();

    SerialDocument doc = SerialDocument::parse("{}");
    ReadNode root = doc.rootView();
    ProjectEvents::onProjectRegister().publish(root);
    ProjectEvents::onProjectRegisterComplete().publish();

    return project;
}

} // namespace Rapture
