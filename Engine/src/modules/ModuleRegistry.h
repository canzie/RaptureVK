#ifndef RAPTURE__MODULE_REGISTRY_H
#define RAPTURE__MODULE_REGISTRY_H

#include "modules/ModuleClass.h"

#include <memory>
#include <string_view>

namespace Rapture {

using ModuleFactory = std::unique_ptr<ModuleClass> (*)();

/**
 * @brief Maps the class name a module asset holds to the class that constructs it.
 *
 * The engine is a static library, so classes register from one explicit list rather than from
 * per-class static objects: a class only ever reached through this registry has nothing referencing
 * its object file, and the linker would drop it along with its registration.
 */
class ModuleRegistry {
  public:
    /**
     * @brief Registers every module class the engine ships
     */
    static void init();
    static void shutdown();

    /**
     * @brief Registers one class under its own type name
     */
    template <typename T>
    static void add()
    {
        addFactory(T::staticType(), []() -> std::unique_ptr<ModuleClass> { return std::make_unique<T>(); });
    }

    /**
     * @brief Constructs the class a module asset names
     * @param className The class name read from the asset
     * @return The new module, or nullptr if no class is registered under that name
     */
    static std::unique_ptr<ModuleClass> create(std::string_view className);

    /**
     * @brief The type registered under a name, for filtering assets without loading them
     * @param className The class name to look up
     * @return The type, or nullptr if no class is registered under that name
     */
    static const TypeInfo *find(std::string_view className);

    /**
     * @brief Whether a class is registered under a name
     */
    static bool contains(std::string_view className);

  private:
    /**
     * @brief Stores one factory under its class name, warning if the name is already taken
     * @param type The type to register, which must outlive the registry
     * @param factory The factory to call for that type
     */
    static void addFactory(const TypeInfo &type, ModuleFactory factory);
};

} // namespace Rapture

#endif // RAPTURE__MODULE_REGISTRY_H
