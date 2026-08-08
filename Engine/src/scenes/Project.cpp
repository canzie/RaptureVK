#include "Project.h"

#include "asset_manager/AssetManager.h"
#include "events/GameEvents.h"
#include "events/ProjectEvents.h"
#include "logging/Log.h"

#include <fstream>

namespace Rapture {

static constexpr std::string_view KEY_METADATA = "metadata";
static constexpr std::string_view KEY_FORMAT_VERSION = "formatVersion";
static constexpr std::string_view KEY_NAME = "name";
static constexpr std::string_view KEY_STARTUP_WORLD = "startupWorld";

static constexpr const char *DEFAULT_WORLD_NAME = "DefaultWorld";

std::unique_ptr<Project> Project::empty()
{
    return std::unique_ptr<Project>(new Project());
}

Project::Project(const std::filesystem::path &projectDirectory, std::string_view name)
    : m_config{std::string(name), projectDirectory}
{
    RP_CORE_INFO("Opening project '{}' at '{}'", m_config.name, m_config.projectDirectory.string());

    createProjectDirectories();
}

void Project::createDefaultWorld()
{
    World *world = createWorld(DEFAULT_WORLD_NAME);
    if (world == nullptr) {
        return;
    }

    world->getScene()->addDefaultContent();
    m_config.startupWorld = m_worlds.back().ref().get()->getHandle();
    activateWorld(world);
}

World *Project::createWorld(std::string name)
{
    AssetPtr<World> world(AssetManager::importAsset(AssetImportDataRequest{
        .data = WorldImportData{std::make_unique<World>(name)},
        .output = getContentDirectory(),
        .name = name,
    }));
    if (!world) {
        RP_CORE_ERROR("Could not create world '{}'", name);
        return nullptr;
    }

    m_worlds.push_back(std::move(world));
    return m_worlds.back().get();
}

World *Project::openWorld(AssetHandle handle)
{
    if (handle == INVALID_ASSET_HANDLE) {
        return nullptr;
    }

    AssetPtr<World> world(AssetManager::getAsset(handle));
    if (!world) {
        RP_CORE_ERROR("asset {} is not a world", handle);
        return nullptr;
    }

    // the manager hands out one payload per handle, so an already open world comes back as itself
    for (const AssetPtr<World> &open : m_worlds) {
        if (open.get() == world.get()) {
            return open.get();
        }
    }

    m_worlds.push_back(std::move(world));
    return m_worlds.back().get();
}

void Project::activateWorld(World *world)
{
    if (world == nullptr) {
        return;
    }

    world->setActive(true);
    if (Scene *scene = world->getScene()) {
        scene->active = true;
    }

    GameEvents::onWorldActivated().publish(world);
}

void Project::deactivateWorld(World *world)
{
    if (world == nullptr) {
        return;
    }

    world->setActive(false);
    if (Scene *scene = world->getScene()) {
        scene->active = false;
    }
}

bool Project::saveWorld(AssetHandle handle)
{
    return AssetManager::saveAsset(handle, getContentDirectory());
}

void Project::onUpdate(float dt)
{
    for (const AssetPtr<World> &world : m_worlds) {
        if (world->isActive()) {
            world->onUpdate(dt);
        }
    }
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
    metadata.set(KEY_STARTUP_WORLD, m_config.startupWorld);

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
    m_config.startupWorld = metadata.child(KEY_STARTUP_WORLD).asU64(INVALID_ASSET_HANDLE);

    ProjectEvents::onProjectRegister().publish(root);
    ProjectEvents::onProjectRegisterComplete().publish();

    openStartupWorld();

    RP_CORE_INFO("Loaded project '{}' from '{}'", m_config.name, path.string());
    return true;
}

void Project::openStartupWorld()
{
    World *world = openWorld(m_config.startupWorld);
    if (world == nullptr) {
        RP_CORE_WARN("no startup world to open, starting from an empty one");
        createDefaultWorld();
        return;
    }

    activateWorld(world);
}

} // namespace Rapture
