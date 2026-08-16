#ifndef RAPTURE__INSTANCE_REGISTRY_H
#define RAPTURE__INSTANCE_REGISTRY_H

#include "scene/instances/SceneComponent.h"
#include "scene/instances/SceneObject.h"

#include <memory>
#include <span>
#include <string_view>

namespace Rapture {

class Scene;

using SceneObjectFactory = std::unique_ptr<SceneObject> (*)(Scene &, std::string_view);
using SceneComponentFactory = std::unique_ptr<SceneComponent> (*)(Scene &, std::string_view);

/**
 * @brief Maps the class name a document holds to the class that constructs it.
 *
 * Scene objects and scene components are registered apart, so the outliner's add menu and the
 * component panel each offer only what belongs in them.
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
     * @brief Registers one scene object class under its own type name
     */
    template <typename T>
    static void addObject()
    {
        addObjectFactory(T::staticType(), [](Scene &scene, std::string_view name) -> std::unique_ptr<SceneObject> {
            return std::make_unique<T>(scene, name);
        });
    }

    /**
     * @brief Registers one scene component class under its own type name
     */
    template <typename T>
    static void addComponent()
    {
        addComponentFactory(T::staticType(), [](Scene &scene, std::string_view name) -> std::unique_ptr<SceneComponent> {
            return std::make_unique<T>(scene, name);
        });
    }

    /**
     * @brief Constructs the scene object class a document names
     * @param className The class name read from the document
     * @param scene The scene the new object belongs to
     * @param name The new object's name
     * @return The new object, or nullptr if no class is registered under that name
     */
    static std::unique_ptr<SceneObject> createObject(std::string_view className, Scene &scene, std::string_view name);

    /**
     * @brief Constructs the scene component class a document names
     * @param className The class name read from the document
     * @param scene The scene the new component belongs to
     * @param name The new component's name
     * @return The new component, or nullptr if no class is registered under that name
     */
    static std::unique_ptr<SceneComponent> createComponent(std::string_view className, Scene &scene, std::string_view name);

    /**
     * @brief Whether a scene object class is registered under a name
     */
    static bool containsObject(std::string_view className);

    /**
     * @brief Whether a scene component class is registered under a name
     */
    static bool containsComponent(std::string_view className);

    /**
     * @brief The type of every registered scene object class, for the outliner's add menu
     */
    static std::span<const TypeInfo *const> objectClasses();

    /**
     * @brief The type of every registered scene component class, for the component panel
     */
    static std::span<const TypeInfo *const> componentClasses();

    /**
     * @brief Looks up a registered class by name, whichever tier it is in
     * @param className The class name to look up
     * @return The type, or nullptr if nothing is registered under that name
     */
    static const TypeInfo *find(std::string_view className);

  private:
    /**
     * @brief Stores one scene object factory, warning if the name is already taken
     * @param type The class to register, which must outlive the registry
     * @param factory The factory to call for that class
     */
    static void addObjectFactory(const TypeInfo &type, SceneObjectFactory factory);

    /**
     * @brief Stores one scene component factory, warning if the name is already taken
     * @param type The class to register, which must outlive the registry
     * @param factory The factory to call for that class
     */
    static void addComponentFactory(const TypeInfo &type, SceneComponentFactory factory);
};

} // namespace Rapture

#endif // RAPTURE__INSTANCE_REGISTRY_H
