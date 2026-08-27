#include "LuaRuntime.h"

#include "core/utils/Log.h"
#include "scene/instances/SceneObject.h"
#include "scene/instances/scene_components/ScriptComponent.h"
#include "scripting/lua/LuaEventSignal.h"
#include "scripting/lua/LuaHandle.h"
#include "scripting/lua/bindings/LuaBindings.h"

#include <sol/sol.hpp>

namespace Rapture::scripting {

struct LuaRuntime::Impl {
    sol::state lua;
    LuaEventConnectionStore connections;
    std::vector<sol::environment> environments;
};

LuaRuntime::LuaRuntime(Scene &scene) : m_impl(std::make_unique<Impl>())
{
    sol::state &lua = m_impl->lua;
    setLuaScene(lua, scene);

    lua.open_libraries(sol::lib::base, sol::lib::coroutine, sol::lib::math, sol::lib::string,
                       sol::lib::table, sol::lib::utf8);

    lua["load"] = sol::lua_nil;
    lua["loadfile"] = sol::lua_nil;
    lua["dofile"] = sol::lua_nil;

    setLuaEventConnectionStore(lua, m_impl->connections);

    registerMathBindings(lua);
    registerEventSignalBindings(lua);
    registerInstanceBindings(lua);
}

LuaRuntime::~LuaRuntime() = default;

lua_State *LuaRuntime::state() const
{
    return m_impl->lua.lua_state();
}

void LuaRuntime::runScript(ScriptComponent &component)
{
    std::string_view source = component.source();
    if (source.empty()) {
        return;
    }

    sol::state &lua = m_impl->lua;

    sol::environment environment(lua, sol::create, lua.globals());
    environment["script"] = LuaHandleBase::push(lua, &component);

    SceneObject *owner = component.owner();
    std::string chunkName = "@" + std::string(owner != nullptr ? owner->name() : component.name());

    sol::protected_function_result result =
        lua.safe_script(source, environment, sol::script_pass_on_error, chunkName);
    if (!result.valid()) {
        sol::error failure = result;
        RP_CORE_ERROR("{}", failure.what());
        return;
    }

    m_impl->environments.push_back(std::move(environment));
}

} // namespace Rapture::scripting
