#ifndef RAPTURE__LUARUNTIME_H
#define RAPTURE__LUARUNTIME_H

#include <memory>

struct lua_State;

namespace Rapture {
class Scene;
class ScriptComponent;
} // namespace Rapture

namespace Rapture::scripting {

class LuaRuntime {
  public:
    /**
     * @brief Opens a state whose scripts reach one scene
     * @param scene The scene, which has to outlive this runtime
     */
    explicit LuaRuntime(Scene &scene);
    ~LuaRuntime();

    LuaRuntime(const LuaRuntime &) = delete;
    LuaRuntime &operator=(const LuaRuntime &) = delete;

    lua_State *state() const;

    /**
     * @brief Runs a script's body, which is where it connects to what it wants to hear
     * @param component The script to run
     */
    void runScript(ScriptComponent &component);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Rapture::scripting

#endif // RAPTURE__LUARUNTIME_H
