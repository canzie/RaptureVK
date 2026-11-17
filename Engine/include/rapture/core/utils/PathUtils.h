#ifndef RAPTURE__PATH_UTILS_H
#define RAPTURE__PATH_UTILS_H

#include <filesystem>
#include <system_error>

namespace Rapture {

/**
 * @brief Expresses a path relative to a folder, so it survives the two being moved together
 * @param path The path to express
 * @param folder The folder it becomes relative to
 * @return The relative path, or the path unchanged if it is already relative or has no relative form
 */
inline std::filesystem::path Path_relativeToFolder(const std::filesystem::path &path, const std::filesystem::path &folder)
{
    if (path.empty() || path.is_relative()) {
        return path;
    }

    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(path, folder, ec);
    return ec || relative.empty() ? path : relative;
}

/**
 * @brief Resolves a path that may be relative to a folder
 * @param path The path to resolve
 * @param folder The folder a relative path is relative to
 * @return The path under the folder, or the path unchanged if it is absolute or empty
 */
inline std::filesystem::path Path_resolveFromFolder(const std::filesystem::path &path, const std::filesystem::path &folder)
{
    if (path.empty() || path.is_absolute()) {
        return path;
    }

    return (folder / path).lexically_normal();
}

} // namespace Rapture

#endif // RAPTURE__PATH_UTILS_H
