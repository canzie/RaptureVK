#include "LuaBindings.h"
#include "LuaInstanceMembers.h"

#include "scene/Scene.h"
#include "scene/instances/Folder.h"

namespace Rapture::scripting {

void registerInstanceBindings(sol::state_view lua)
{
    auto instance = lua.new_usertype<LuaHandle<Instance>>("Instance", sol::no_constructor, sol::base_classes,
                                                          sol::bases<LuaHandleBase>());
    addInstanceMembers(instance);
    LuaHandleBase::registerType<Instance>();

    instance["new"] = [](const std::string &className, sol::optional<sol::object> parentValue,
                         sol::this_state state) {
        sol::state_view lua(state);
        Scene *scene = luaScene(lua);
        if (scene == nullptr) {
            luaL_error(state, "this state reaches no scene");
        }

        SceneObject *parent = scene->root();
        if (parentValue.has_value() && parentValue->valid()) {
            parent = resolveInstanceArgument(*parentValue, state)->as<SceneObject>();
            if (parent == nullptr) {
                luaL_error(state, "a parent has to be a scene object");
            }
        }

        std::unique_ptr<SceneObject> created = InstanceRegistry::createObject(className, *scene, className);
        if (created == nullptr) {
            luaL_error(state, "no scene object class is registered under that name");
        }

        SceneObject *raw = created.get();
        parent->addChild(std::move(created));
        raw->ready();
        return LuaHandleBase::push(lua, raw);
    };

    auto sceneObject = lua.new_usertype<LuaHandle<SceneObject>>(
        "SceneObject", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addSceneObjectMembers(sceneObject);
    LuaHandleBase::registerType<SceneObject>();

    auto folder = lua.new_usertype<LuaHandle<Folder>>("Folder", sol::no_constructor, sol::base_classes,
                                                      sol::bases<LuaHandleBase>());
    addSceneObjectMembers(folder);
    LuaHandleBase::registerType<Folder>();
}

} // namespace Rapture::scripting
