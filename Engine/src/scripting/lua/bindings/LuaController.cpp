#include "LuaBindings.h"
#include "LuaInstanceMembers.h"

#include "scene/instances/controllers/CameraController.h"
#include "scene/instances/controllers/PlayerController.h"

namespace Rapture::scripting {

void registerControllerBindings(sol::state_view lua)
{
    auto controller = lua.new_usertype<LuaHandle<Controller>>(
        "Controller", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addControllerMembers(controller);
    LuaHandleBase::registerType<Controller>();

    auto playerController = lua.new_usertype<LuaHandle<PlayerController>>(
        "PlayerController", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addControllerMembers(playerController);
    LuaHandleBase::registerType<PlayerController>();

    auto cameraController = lua.new_usertype<LuaHandle<CameraController>>(
        "CameraController", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addControllerMembers(cameraController);
    LuaHandleBase::registerType<CameraController>();
}

} // namespace Rapture::scripting
