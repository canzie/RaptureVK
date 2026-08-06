#include "Project.h"

#include "events/ProjectEvents.h"
#include "logging/Log.h"

#include <fstream>

namespace Rapture {

static constexpr std::string_view KEY_METADATA = "metadata";
static constexpr std::string_view KEY_FORMAT_VERSION = "formatVersion";
static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_INITIAL_WORLD = "initialWorld";
static constexpr std::string_view KEY_STARTUP_SCENE = "startupScene";

std::unique_ptr<Project> Project::empty()
{
    return std::unique_ptr<Project>(new Project());
}

Project::Project(const std::filesystem::path &projectDirectory, std::string_view name)
    : m_config{std::string(name), projectDirectory, "DefaultWorld"}
{
    RP_CORE_INFO("Opening project '{}' at '{}'", m_config.name, m_config.projectDirectory.string());

    createProjectDirectories();
}

void Project::createDefaultWorld()
{
    World *defaultWorld = m_sceneManager.createWorld(m_config.initialWorldName);
    auto defaultScene = m_sceneManager.createScene(RAPTURE_DEFAULT_SCENE_NAME);
    defaultScene->addDefaultContent();
    m_sceneManager.activateScene(RAPTURE_DEFAULT_SCENE_NAME);

    defaultWorld->addScene(RAPTURE_DEFAULT_SCENE_NAME, defaultScene);
    defaultWorld->setMainScene(RAPTURE_DEFAULT_SCENE_NAME);

    m_sceneManager.setActiveWorld(m_config.initialWorldName);
}

void Project::createProjectDirectories()
{
    std::error_code ec;
    std::filesystem::create_directories(getBlobDirectory(), ec);
    std::filesystem::create_directories(getThumbnailDirectory(), ec);
    std::filesystem::create_directories(getContentDirectory(), ec);

    if (ec) {
        RP_CORE_ERROR("Failed to create project directories at '{0}': {1}", m_config.projectDirectory.string(), ec.message());
        return;
    }

    RP_CORE_INFO("Project directory: {0}", m_config.projectDirectory.string());
}

bool Project::saveProject(const std::filesystem::path &path)
{
    SerialDocument doc;
    WriteNode root = doc.root();

    WriteNode metadata = root.addObject(KEY_METADATA);
    metadata.set(KEY_FORMAT_VERSION, static_cast<uint64_t>(PROJECT_FORMAT_VERSION));
    metadata.set(KEY_NAME, std::string_view(m_config.name));
    metadata.set(KEY_INITIAL_WORLD, std::string_view(m_config.initialWorldName));
    metadata.set(KEY_STARTUP_SCENE, m_config.startupScene);

    ProjectEvents::onProjectSerialize().publish(root);

    std::string text = doc.toText(true);
    if (text.empty()) {
        RP_CORE_ERROR("Failed to serialize project '{}'", m_config.name);
        return false;
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            RP_CORE_ERROR("Failed to create the directory for '{}': {}", path.string(), ec.message());
            return false;
        }
    }

    std::filesystem::path tempPath = path;
    tempPath += ".tmp";

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            RP_CORE_ERROR("Failed to open '{}' for writing", tempPath.string());
            return false;
        }

        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out.good()) {
            RP_CORE_ERROR("Failed to write '{}'", tempPath.string());
            return false;
        }
    }

    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        RP_CORE_ERROR("Failed to move '{}' onto '{}': {}", tempPath.string(), path.string(), ec.message());
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    RP_CORE_INFO("Wrote project '{}' to '{}'", m_config.name, path.string());
    return true;
}

bool Project::loadProject(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        RP_CORE_ERROR("Failed to open '{}'", path.string());
        return false;
    }

    std::string text(static_cast<size_t>(in.tellg()), '\0');
    in.seekg(0);
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!in.good()) {
        RP_CORE_ERROR("Failed to read '{}'", path.string());
        return false;
    }

    SerialDocument doc = SerialDocument::parse(text);
    ReadNode root = doc.rootView();
    ReadNode metadata = root.child(KEY_METADATA);

    uint32_t formatVersion = static_cast<uint32_t>(metadata.child(KEY_FORMAT_VERSION).asU64(0));
    if (formatVersion != PROJECT_FORMAT_VERSION) {
        RP_CORE_ERROR("cannot read a version {} project, this build reads version {}", formatVersion, PROJECT_FORMAT_VERSION);
        return false;
    }

    m_config.name = metadata.child(KEY_NAME).asString(m_config.name);
    m_config.initialWorldName = metadata.child(KEY_INITIAL_WORLD).asString(m_config.initialWorldName);
    m_config.startupScene = metadata.child(KEY_STARTUP_SCENE).asU64(INVALID_ASSET_HANDLE);

    ProjectEvents::onProjectRegister().publish(root);
    ProjectEvents::onProjectRegisterComplete().publish();

    openStartupScene();

    RP_CORE_INFO("Loaded project '{}' from '{}'", m_config.name, path.string());
    return true;
}

void Project::openStartupScene()
{
    Scene *scene = m_sceneManager.openScene(m_config.startupScene);
    if (scene == nullptr) {
        RP_CORE_WARN("no startup scene to open, starting from an empty one");
        createDefaultWorld();
        return;
    }

    World *world = m_sceneManager.createWorld(m_config.initialWorldName);
    world->addScene(scene->getSceneName(), scene);
    world->setMainScene(scene->getSceneName());

    m_sceneManager.setActiveWorld(m_config.initialWorldName);
    m_sceneManager.activateScene(scene);
}

} // namespace Rapture
