#include "EditorConfig.h"

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/core/utils/EnginePaths.h>
#include <rapture/core/utils/Log.h>
#include <rapture/core/utils/rp_assert.h>

#include <fstream>

static constexpr std::string_view CONFIG_FILE_NAME = "launch.cfg";
static constexpr size_t MAX_RECENT_PROJECTS = 10;

static constexpr std::string_view KEY_AUTO_LAUNCH = "autoLaunch";
static constexpr std::string_view KEY_RECENT = "recent";
static constexpr std::string_view KEY_SETTINGS = "settings";

static std::filesystem::path s_configPath()
{
    return Rapture::EnginePaths::executableDirectory() / CONFIG_FILE_NAME;
}

EditorConfig::EditorConfig()
{
    for (size_t i = 0; i < EDITOR_CONFIG_COUNT; i++) {
        m_values[i] = EDITOR_CONFIG_TABLE[i].fallback;
    }
}

float EditorConfig::getFloat(EditorConfigKey key) const
{
    RP_ASSERT(EDITOR_CONFIG_TABLE[key].type == CONFIG_VALUE_FLOAT, "config entry is not a float");
    return static_cast<float>(std::get<double>(m_values[key]));
}

int64_t EditorConfig::getInt(EditorConfigKey key) const
{
    RP_ASSERT(EDITOR_CONFIG_TABLE[key].type == CONFIG_VALUE_INT, "config entry is not an int");
    return std::get<int64_t>(m_values[key]);
}

bool EditorConfig::getBool(EditorConfigKey key) const
{
    RP_ASSERT(EDITOR_CONFIG_TABLE[key].type == CONFIG_VALUE_BOOL, "config entry is not a bool");
    return std::get<bool>(m_values[key]);
}

void EditorConfig::set(EditorConfigKey key, ConfigValue value)
{
    RP_ASSERT(value.index() == m_values[key].index(), "config value does not hold the type its entry declares");
    m_values[key] = value;
}

void EditorConfig::addRecentProject(const std::filesystem::path &projectPath)
{
    if (projectPath.empty()) {
        return;
    }

    std::erase(m_recentProjects, projectPath);
    m_recentProjects.insert(m_recentProjects.begin(), projectPath);

    if (m_recentProjects.size() > MAX_RECENT_PROJECTS) {
        m_recentProjects.resize(MAX_RECENT_PROJECTS);
    }
}

EditorConfig EditorConfig::load()
{
    EditorConfig config;

    std::filesystem::path path = s_configPath();
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return config;
    }

    std::string text(static_cast<size_t>(in.tellg()), '\0');
    in.seekg(0);
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!in.good()) {
        RP_WARN("Failed to read '{}'", path.string());
        return config;
    }

    Rapture::SerialDocument doc = Rapture::SerialDocument::parse(text);
    Rapture::ReadNode root = doc.rootView();

    std::string_view autoLaunch = root.child(KEY_AUTO_LAUNCH).asString();
    if (!autoLaunch.empty()) {
        config.m_autoLaunchProject = std::filesystem::path(autoLaunch);
    }

    Rapture::ReadNode recent = root.child(KEY_RECENT);
    for (size_t i = 0; i < recent.size(); i++) {
        std::string_view entry = recent.at(i).asString();
        if (!entry.empty()) {
            config.m_recentProjects.emplace_back(entry);
        }
    }

    Rapture::ReadNode settings = root.child(KEY_SETTINGS);
    for (size_t i = 0; i < EDITOR_CONFIG_COUNT; i++) {
        const EditorConfigEntry &entry = EDITOR_CONFIG_TABLE[i];
        Rapture::ReadNode stored = settings.child(entry.key);
        if (!stored.valid()) {
            continue;
        }

        switch (entry.type) {
        case CONFIG_VALUE_FLOAT:
            config.m_values[i] = stored.asF64(std::get<double>(entry.fallback));
            break;
        case CONFIG_VALUE_INT:
            config.m_values[i] = stored.asI64(std::get<int64_t>(entry.fallback));
            break;
        case CONFIG_VALUE_BOOL:
            config.m_values[i] = stored.asBool(std::get<bool>(entry.fallback));
            break;
        case CONFIG_VALUE_COUNT:
            break;
        }
    }

    return config;
}

bool EditorConfig::save() const
{
    Rapture::SerialDocument doc;
    Rapture::WriteNode root = doc.root();

    if (!m_autoLaunchProject.empty()) {
        root.set(KEY_AUTO_LAUNCH, m_autoLaunchProject.string());
    }

    Rapture::WriteNode recent = root.addArray(KEY_RECENT);
    for (const std::filesystem::path &project : m_recentProjects) {
        recent.append(project.string());
    }

    Rapture::WriteNode settings = root.addObject(KEY_SETTINGS);
    for (size_t i = 0; i < EDITOR_CONFIG_COUNT; i++) {
        const EditorConfigEntry &entry = EDITOR_CONFIG_TABLE[i];
        switch (entry.type) {
        case CONFIG_VALUE_FLOAT:
            settings.set(entry.key, std::get<double>(m_values[i]));
            break;
        case CONFIG_VALUE_INT:
            settings.set(entry.key, std::get<int64_t>(m_values[i]));
            break;
        case CONFIG_VALUE_BOOL:
            settings.set(entry.key, std::get<bool>(m_values[i]));
            break;
        case CONFIG_VALUE_COUNT:
            break;
        }
    }

    std::string text = doc.toText(true);
    if (text.empty()) {
        RP_ERROR("Failed to serialize the editor settings");
        return false;
    }

    std::filesystem::path path = s_configPath();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        RP_ERROR("Failed to open '{}' for writing", path.string());
        return false;
    }

    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) {
        RP_ERROR("Failed to write '{}'", path.string());
        return false;
    }

    return true;
}
