#include "LuaHandle.h"

#include "core/utils/TypeInfo.h"

namespace Rapture::scripting {

static constexpr const char *SCENE_REGISTRY_KEY = "rapture.scene";

std::vector<LuaHandleBase::Push> LuaHandleBase::s_pushByTypeId;

void setLuaScene(sol::state_view lua, Scene &scene)
{
    lua.registry()[SCENE_REGISTRY_KEY] = sol::light(scene);
}

Scene *luaScene(sol::state_view lua)
{
    sol::optional<sol::light<Scene>> scene = lua.registry()[SCENE_REGISTRY_KEY].get<sol::optional<sol::light<Scene>>>();
    if (!scene.has_value()) {
        return nullptr;
    }

    return scene->value();
}

LuaHandleBase::LuaHandleBase(Instance *object) : m_object(object)
{
    if (object != nullptr) {
        m_alive = object->aliveFlag();
    }
}

Instance *LuaHandleBase::get() const
{
    if (m_alive == nullptr || !*m_alive) {
        return nullptr;
    }

    return m_object;
}

void LuaHandleBase::registerType(const TypeInfo &type, Push push)
{
    if (s_pushByTypeId.size() <= type.id) {
        s_pushByTypeId.resize(type.id + 1, nullptr);
    }

    s_pushByTypeId[type.id] = push;
}

sol::object LuaHandleBase::push(sol::state_view lua, Instance *instance)
{
    if (instance == nullptr) {
        return sol::make_object(lua, sol::lua_nil);
    }

    const TypeInfo &type = instance->type();
    if (type.id >= s_pushByTypeId.size() || s_pushByTypeId[type.id] == nullptr) {
        return sol::make_object(lua, sol::lua_nil);
    }

    return s_pushByTypeId[type.id](lua, instance);
}

} // namespace Rapture::scripting
