#include "LuaBindings.h"
#include "LuaInstanceMembers.h"

#include "scene/instances/Camera3D.h"
#include "scene/instances/Mesh3D.h"
#include "scene/instances/SkeletalMesh3D.h"
#include "scene/instances/SpringArm3D.h"
#include "scene/instances/StaticMesh3D.h"

namespace Rapture::scripting {

void registerNode3DBindings(sol::state_view lua)
{
    auto node3D = lua.new_usertype<LuaHandle<Node3D>>("Node3D", sol::no_constructor, sol::base_classes,
                                                      sol::bases<LuaHandleBase>());
    addNode3DMembers(node3D);
    LuaHandleBase::registerType<Node3D>();

    auto camera = lua.new_usertype<LuaHandle<Camera3D>>("Camera3D", sol::no_constructor, sol::base_classes,
                                                        sol::bases<LuaHandleBase>());
    addNode3DMembers(camera);
    LuaHandleBase::registerType<Camera3D>();

    auto mesh = lua.new_usertype<LuaHandle<Mesh3D>>("Mesh3D", sol::no_constructor, sol::base_classes,
                                                    sol::bases<LuaHandleBase>());
    addNode3DMembers(mesh);
    LuaHandleBase::registerType<Mesh3D>();

    auto staticMesh = lua.new_usertype<LuaHandle<StaticMesh3D>>(
        "StaticMesh3D", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addNode3DMembers(staticMesh);
    LuaHandleBase::registerType<StaticMesh3D>();

    auto skeletalMesh = lua.new_usertype<LuaHandle<SkeletalMesh3D>>(
        "SkeletalMesh3D", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addNode3DMembers(skeletalMesh);
    LuaHandleBase::registerType<SkeletalMesh3D>();

    auto springArm = lua.new_usertype<LuaHandle<SpringArm3D>>(
        "SpringArm3D", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    addNode3DMembers(springArm);
    springArm["length"] = sol::property(
        [](const LuaHandle<SpringArm3D> &self, sol::this_state state) { return resolveHandle(self, state)->length(); },
        [](LuaHandle<SpringArm3D> &self, float length, sol::this_state state) {
            resolveHandle(self, state)->setLength(length);
        });
    springArm["followsControlRotation"] = sol::property(
        [](const LuaHandle<SpringArm3D> &self, sol::this_state state) {
            return resolveHandle(self, state)->followsControlRotation();
        },
        [](LuaHandle<SpringArm3D> &self, bool follows, sol::this_state state) {
            resolveHandle(self, state)->setFollowsControlRotation(follows);
        });
    LuaHandleBase::registerType<SpringArm3D>();
}

} // namespace Rapture::scripting
