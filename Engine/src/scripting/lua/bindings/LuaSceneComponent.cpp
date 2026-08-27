#include "LuaBindings.h"
#include "LuaInstanceMembers.h"

namespace Rapture::scripting {

void registerSceneComponentBindings(sol::state_view lua)
{
    auto sceneComponent = lua.new_usertype<LuaHandle<SceneComponent>>(
        "SceneComponent", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addSceneComponentMembers(sceneComponent);
    LuaHandleBase::registerType<SceneComponent>();

    auto scriptComponent = lua.new_usertype<LuaHandle<ScriptComponent>>(
        "ScriptComponent", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addSceneComponentMembers(scriptComponent);
    LuaHandleBase::registerType<ScriptComponent>();

    auto characterBody = lua.new_usertype<LuaHandle<CharacterBody3D>>(
        "CharacterBody3D", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addCharacterBody3DMembers(characterBody);
    LuaHandleBase::registerType<CharacterBody3D>();
}

} // namespace Rapture::scripting
