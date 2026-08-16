#include "LauncherConfig.h"

#include "core/utils/Log.h"
#include "core/serialization/SerialDocument.h"
#include "core/utils/EnginePaths.h"

#include <algorithm>
#include <fstream>
#include <string>

static constexpr std::string_view CONFIG_FILE_NAME = "launch.cfg";
static constexpr size_t MAX_RECENT_PROJECTS = 10;

static constexpr std::string_view KEY_AUTO_LAUNCH = "autoLaunch";
static constexpr std::string_view KEY_RECENT = "recent";

static std::filesystem::path s_configPath()
{
    return Rapture::EnginePaths::executableDirectory() / CONFIG_FILE_NAME;
}

LauncherConfig LauncherConfig::load()
{
    LauncherConfig config;

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

    return config;
}

bool LauncherConfig::save() const
{
    Rapture::SerialDocument doc;
    Rapture::WriteNode root = doc.root();

    root.set(KEY_AUTO_LAUNCH, m_autoLaunchProject.string());

    Rapture::WriteNode recent = root.addArray(KEY_RECENT);
    for (const auto &project : m_recentProjects) {
        recent.append(project.string());
    }

    std::string text = doc.toText(true);
    if (text.empty()) {
        RP_ERROR("Failed to serialize the launcher settings");
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

void LauncherConfig::addRecentProject(const std::filesystem::path &projectPath)
{
    if (projectPath.empty()) {
        return;
    }

    auto existing = std::find(m_recentProjects.begin(), m_recentProjects.end(), projectPath);
    if (existing != m_recentProjects.end()) {
        m_recentProjects.erase(existing);
    }

    m_recentProjects.insert(m_recentProjects.begin(), projectPath);

    if (m_recentProjects.size() > MAX_RECENT_PROJECTS) {
        m_recentProjects.resize(MAX_RECENT_PROJECTS);
    }
}
