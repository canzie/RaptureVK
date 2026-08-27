#ifndef RAPTURE__LUAHANDLE_H
#define RAPTURE__LUAHANDLE_H

#include "scene/instances/Instance.h"

#include <sol/sol.hpp>

#include <memory>
#include <vector>

namespace Rapture {
class Scene;
}

namespace Rapture::scripting {

/**
 * @brief Names the scene a state's scripts reach
 * @param lua The state to install into
 * @param scene The scene, which has to outlive the state
 */
void setLuaScene(sol::state_view lua, Scene &scene);

/**
 * @brief The scene a state's scripts reach
 * @param lua The state to read from
 * @return The scene, or nullptr if none was installed
 */
Scene *luaScene(sol::state_view lua);

/**
 * @brief The part of a handle that does not depend on which class is being reached
 */
class LuaHandleBase {
  public:
    /**
     * @brief Builds the handle a script reaches an instance of one class through
     */
    using Push = sol::object (*)(sol::state_view lua, Instance *instance);

    LuaHandleBase() = default;

    /**
     * @brief Takes an unowned reference to an instance
     * @param object The instance to reach, which may be null
     */
    explicit LuaHandleBase(Instance *object);

    /**
     * @brief The instance this refers to
     * @return The instance, or nullptr once it has been destroyed
     */
    Instance *get() const;

    /**
     * @brief Names the handle a class is reached through, for as long as the process runs
     * @param type The class being bound
     * @param push Builds the handle for an instance of that class
     */
    static void registerType(const TypeInfo &type, Push push);

    /**
     * @brief Names the handle a class is reached through, for as long as the process runs
     */
    template <typename T>
    static void registerType();

    /**
     * @brief Pushes the handle for what an instance actually is, rather than for the class it is
     *        held as
     * @param lua The state to push onto
     * @param instance The instance to reach, which may be null
     * @return The handle, or nil if the instance is null or its class was never registered
     */
    static sol::object push(sol::state_view lua, Instance *instance);

  protected:
    Instance *m_object = nullptr;
    std::shared_ptr<bool> m_alive;

  private:
    static std::vector<Push> s_pushByTypeId;
};

/**
 * @brief What a script reaches an instance of one class through
 */
template <typename T>
class LuaHandle : public LuaHandleBase {
  public:
    LuaHandle() = default;
    explicit LuaHandle(T *object) : LuaHandleBase(object) {}

    /**
     * @brief The instance this refers to
     * @return The instance, or nullptr once it has been destroyed
     */
    T *get() const { return static_cast<T *>(LuaHandleBase::get()); }
};

template <typename T>
void LuaHandleBase::registerType()
{
    registerType(T::staticType(), [](sol::state_view lua, Instance *instance) {
        return sol::make_object(lua, LuaHandle<T>(static_cast<T *>(instance)));
    });
}

} // namespace Rapture::scripting

#endif // RAPTURE__LUAHANDLE_H
