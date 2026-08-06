#ifndef RAPTURE__INSTANCE_REGISTRY_H
#define RAPTURE__INSTANCE_REGISTRY_H

#include "scenes/instances/Instance.h"

#include <memory>
#include <string_view>

namespace Rapture {

class Scene;

using InstanceFactory = std::unique_ptr<Instance> (*)(Scene &, std::string_view);

/**
 * @brief Maps the class name a scene file holds to the class that constructs it.
 *
 * The engine is a static library, so classes register from one explicit list rather than from
 * per-class static objects: a class only ever reached through this registry has nothing referencing
 * its object file, and the linker would drop it along with its registration.
 */
class InstanceRegistry {
  public:
    /**
     * @brief Registers every class the engine ships
     */
    static void init();
    static void shutdown();

    /**
     * @brief Registers one class under its own type name
     */
    template <typename T>
    static void add()
    {
        addFactory(T::staticType().name, [](Scene &scene, std::string_view name) -> std::unique_ptr<Instance> {
            return std::make_unique<T>(scene, name);
        });
    }

    /**
     * @brief Constructs the class a scene file names
     * @param className The class name read from the file
     * @param scene The scene the new object belongs to
     * @param name The new object's name
     * @return The new object, or nullptr if no class is registered under that name
     */
    static std::unique_ptr<Instance> create(std::string_view className, Scene &scene, std::string_view name);

    /**
     * @brief Whether a class is registered under a name
     */
    static bool contains(std::string_view className);

  private:
    /**
     * @brief Stores one factory, warning if the name is already taken
     * @param className The name to register under, which must outlive the registry
     * @param factory The factory to call for that name
     */
    static void addFactory(std::string_view className, InstanceFactory factory);
};

} // namespace Rapture

#endif // RAPTURE__INSTANCE_REGISTRY_H
