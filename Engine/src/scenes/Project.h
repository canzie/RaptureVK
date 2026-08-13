#ifndef RAPTURE__PROJECT_H
#define RAPTURE__PROJECT_H

#include "asset_manager/AssetHandle.h"
#include "scenes/Scene.h"
#include "scenes/World.h"
#include "serialization/SerialDocument.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Rapture {

static constexpr uint32_t PROJECT_FORMAT_VERSION = 1;

struct ProjectConfig {
    std::string name;
    std::filesystem::path projectDirectory;

    AssetHandle startupWorld = INVALID_ASSET_HANDLE;
};

class Project {
  public:
    /**
     * @brief The empty project the editor runs on before one is opened, which touches no disk
     * @return An empty project, whose scene list every caller can iterate without a null check
     */
    static std::unique_ptr<Project> empty();

    /**
     * @brief Opens a project rooted at a directory, creating the directory tree if it is missing
     * @param projectDirectory The directory holding the project file, its content and its cache
     * @param name The project's name
     */
    Project(const std::filesystem::path &projectDirectory, std::string_view name);

    /**
     * @brief Whether this is a real project rather than the empty stand in
     */
    bool isValid() const { return !m_config.projectDirectory.empty(); }

    /**
     * @brief Creates the initial world holding one empty scene, and activates both
     */
    void createDefaultWorld();

    /**
     * @brief Opens a world asset, adding it to the worlds this project is running
     * @param handle The world asset to open
     * @return The world, or nullptr if the asset could not be read
     */
    World *openWorld(AssetHandle handle);

    /**
     * @brief Builds a new empty world and adds it to the worlds this project is running
     * @param name Name of the world
     * @return The world
     */
    World *createWorld(std::string name);

    /**
     * @brief Writes a world back into its asset
     * @param handle The world asset to write
     * @return True if the asset now holds the world's contents
     */
    bool saveWorld(AssetHandle handle);

    /**
     * @brief Starts running a world and the scene it holds
     * @param world The world to activate
     */
    void activateWorld(World *world);

    /**
     * @brief Stops running a world and the scene it holds
     * @param world The world to deactivate
     */
    void deactivateWorld(World *world);

    const std::vector<AssetPtr<World>> &getWorlds() const { return m_worlds; }

    void onUpdate(float dt);

    /**
     * @brief Writes the project file, replacing it atomically
     * @param path Destination .rapt file
     * @return True if the file now holds this project
     */
    bool saveProject(const std::filesystem::path &path);

    /**
     * @brief Reads a project file into this project and opens its startup scene
     * @param path Source .rapt file
     * @return True if the project now holds the file's contents
     */
    bool loadProject(const std::filesystem::path &path);

    AssetHandle getStartupWorld() const { return m_config.startupWorld; }
    void setStartupWorld(AssetHandle startupWorld) { m_config.startupWorld = startupWorld; }

    // Project config access
    std::filesystem::path getProjectDirectory() const { return m_config.projectDirectory; }

    /**
     * @brief The project file this project reads from and writes to
     */
    std::filesystem::path getProjectFilePath() const { return m_config.projectDirectory / (m_config.name + ".rapt"); }
    std::filesystem::path getCacheDirectory() const { return m_config.projectDirectory / ".cache"; }
    std::filesystem::path getBlobDirectory() const { return m_config.projectDirectory / "blobs"; }
    std::filesystem::path getContentDirectory() const { return m_config.projectDirectory / "content"; }
    std::filesystem::path getThumbnailDirectory() const { return getCacheDirectory() / "thumbnails"; }
    std::string getProjectName() const { return m_config.name; }

    void setProjectName(const std::string &name) { m_config.name = name; }

    const ProjectConfig &getConfig() const { return m_config; }

    /**
     * @brief The editor's own state, stored in the project file but never read by it
     * @return Cursor to the section root, invalid when the project holds none
     */
    ReadNode getEditorSection() const { return m_editorSection.rootView(); }

    /**
     * @brief Replaces the editor's state, to be written out with the project
     * @param section A readable document holding whatever the editor wants back
     */
    void setEditorSection(SerialDocument section) { m_editorSection = std::move(section); }

  private:
    Project() = default;

    /**
     * @brief Creates the project directory and its cache subfolders on disk if missing
     */
    void createProjectDirectories();

    /**
     * @brief Opens the configured startup world and makes it active
     */
    void openStartupWorld();

  private:
    ProjectConfig m_config;
    SerialDocument m_editorSection;
    std::vector<AssetPtr<World>> m_worlds;
    SerialDocument m_saveFile;
};
} // namespace Rapture

#endif // RAPTURE__PROJECT_H
