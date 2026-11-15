#ifndef RAPTURE__ENGINE_PATHS_H
#define RAPTURE__ENGINE_PATHS_H

#include <filesystem>

namespace Rapture {

/**
 * @brief The directories the running executable ships with, resolved from its own location.
 */
class EnginePaths {
  public:
    /**
     * @brief Resolves every path from the running executable, before anything loads an asset
     */
    static void init();

    /**
     * @brief The running executable itself
     */
    static const std::filesystem::path &executable();

    /**
     * @brief The directory holding the running executable
     */
    static const std::filesystem::path &executableDirectory();

    /**
     * @brief The root of the assets shipped alongside the executable
     */
    static const std::filesystem::path &assetDirectory();

    /**
     * @brief The engine's own shaders
     */
    static const std::filesystem::path &shaderDirectory();
};

} // namespace Rapture

#endif // RAPTURE__ENGINE_PATHS_H
