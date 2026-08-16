#include "EnginePaths.h"

#include "core/utils/Log.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif // _WIN32

namespace Rapture {

static std::filesystem::path s_executable;
static std::filesystem::path s_executableDirectory;
static std::filesystem::path s_assetDirectory;
static std::filesystem::path s_shaderDirectory;

static std::filesystem::path s_resolveExecutable()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0) {
        RP_CORE_ERROR("could not resolve the running executable");
        return std::filesystem::current_path();
    }

    return std::filesystem::path(buffer, buffer + length);
#else
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        RP_CORE_ERROR("could not resolve the running executable");
        return std::filesystem::current_path();
    }

    buffer[length] = '\0';
    return std::filesystem::path(buffer);
#endif // _WIN32
}

void EnginePaths::init()
{
    s_executable = s_resolveExecutable();
    s_executableDirectory = s_executable.parent_path();
    s_assetDirectory = s_executableDirectory / "assets";
    s_shaderDirectory = s_assetDirectory / "engine/shaders";

    RP_CORE_INFO("Engine assets: {}", s_assetDirectory.string());
}

const std::filesystem::path &EnginePaths::executable()
{
    return s_executable;
}

const std::filesystem::path &EnginePaths::executableDirectory()
{
    return s_executableDirectory;
}

const std::filesystem::path &EnginePaths::assetDirectory()
{
    return s_assetDirectory;
}

const std::filesystem::path &EnginePaths::shaderDirectory()
{
    return s_shaderDirectory;
}

} // namespace Rapture
