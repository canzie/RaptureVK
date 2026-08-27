#ifndef RAPTURE__LUAINSTANCEMEMBERS_H
#define RAPTURE__LUAINSTANCEMEMBERS_H

#include "scene/instances/CharacterBody3D.h"
#include "scene/instances/InstanceRegistry.h"
#include "scene/instances/Node3D.h"
#include "scene/instances/controllers/Controller.h"
#include "scene/instances/scene_components/ScriptComponent.h"
#include "scripting/lua/LuaEventSignal.h"
#include "scripting/lua/LuaHandle.h"

namespace Rapture::scripting {

template <typename T>
T *resolveHandle(const LuaHandle<T> &handle, lua_State *state)
{
    T *object = handle.get();
    if (object == nullptr) {
        luaL_error(state, "instance has been destroyed");
    }

    return object;
}

inline Instance *resolveInstanceArgument(const sol::object &value, lua_State *state)
{
    if (!value.is<LuaHandleBase>()) {
        luaL_error(state, "expected an instance");
    }

    Instance *instance = value.as<LuaHandleBase>().get();
    if (instance == nullptr) {
        luaL_error(state, "instance has been destroyed");
    }

    return instance;
}

inline bool instanceIsA(const Instance &instance, std::string_view className)
{
    const TypeInfo &type = instance.type();
    for (uint8_t i = 0; i <= type.depth; i++) {
        if (type.chain[i]->name == className) {
            return true;
        }
    }

    return false;
}

inline SceneObject *findDescendantOfType(const SceneObject &object, std::string_view className)
{
    for (const auto &child : object.children()) {
        if (instanceIsA(*child, className)) {
            return child.get();
        }
        if (SceneObject *found = findDescendantOfType(*child, className)) {
            return found;
        }
    }

    return nullptr;
}

inline bool isDescendantOf(const SceneObject &object, const SceneObject &ancestor)
{
    for (const SceneObject *walk = &object; walk != nullptr; walk = walk->parent()) {
        if (walk == &ancestor) {
            return true;
        }
    }

    return false;
}

inline void reparentObject(SceneObject &object, SceneObject &parent, lua_State *state)
{
    if (&object == &parent || isDescendantOf(parent, object)) {
        luaL_error(state, "cannot parent an object below itself");
    }

    SceneObject *previous = object.parent();
    if (previous == nullptr) {
        luaL_error(state, "cannot reparent the scene root");
    }

    parent.addChild(previous->removeChild(&object));
}

inline sol::table snapshotHandles(sol::state_view lua, std::span<const std::unique_ptr<SceneObject>> objects)
{
    sol::table result = lua.create_table(static_cast<int>(objects.size()), 0);
    for (const auto &object : objects) {
        sol::object handle = LuaHandleBase::push(lua, object.get());
        if (handle.valid()) {
            result.add(handle);
        }
    }

    return result;
}

inline sol::table snapshotHandles(sol::state_view lua, std::span<const std::unique_ptr<SceneComponent>> components)
{
    sol::table result = lua.create_table(static_cast<int>(components.size()), 0);
    for (const auto &component : components) {
        sol::object handle = LuaHandleBase::push(lua, component.get());
        if (handle.valid()) {
            result.add(handle);
        }
    }

    return result;
}

template <typename T>
void addInstanceMembers(sol::usertype<LuaHandle<T>> &type)
{
    type["id"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return std::to_string(resolveHandle(self, state)->id()); });

    type["name"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return std::string(resolveHandle(self, state)->name()); },
        [](LuaHandle<T> &self, const std::string &name, sol::this_state state) { resolveHandle(self, state)->setName(name); });

    type["isA"] = [](const LuaHandle<T> &self, const std::string &className, sol::this_state state) {
        return instanceIsA(*resolveHandle(self, state), className);
    };

    type["onDestroy"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        Instance *instance = resolveHandle(self, state);
        return makeLuaEventSignal(instance, instance->onDestroy);
    });
}

template <typename T>
void addSceneObjectMembers(sol::usertype<LuaHandle<T>> &type)
{
    addInstanceMembers(type);

    type["parent"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) {
            return LuaHandleBase::push(sol::state_view(state), resolveHandle(self, state)->parent());
        },
        [](LuaHandle<T> &self, const sol::object &value, sol::this_state state) {
            Instance *parent = resolveInstanceArgument(value, state);
            SceneObject *object = parent->as<SceneObject>();
            if (object == nullptr) {
                luaL_error(state, "a parent has to be a scene object");
            }
            reparentObject(*resolveHandle(self, state), *object, state);
        });

    type["children"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return snapshotHandles(sol::state_view(state), resolveHandle(self, state)->children());
    });

    type["components"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return snapshotHandles(sol::state_view(state), resolveHandle(self, state)->components());
    });

    type["findChild"] = [](const LuaHandle<T> &self, const std::string &name, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), resolveHandle(self, state)->findChild(name));
    };

    type["findDescendant"] = [](const LuaHandle<T> &self, const std::string &path, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), resolveHandle(self, state)->findDescendant(path));
    };

    type["findFirstDescendantOfType"] = [](const LuaHandle<T> &self, const std::string &className, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), findDescendantOfType(*resolveHandle(self, state), className));
    };

    type["getComponent"] = [](const LuaHandle<T> &self, const std::string &className, sol::this_state state) {
        for (const auto &component : resolveHandle(self, state)->components()) {
            if (instanceIsA(*component, className)) {
                return LuaHandleBase::push(sol::state_view(state), component.get());
            }
        }
        return LuaHandleBase::push(sol::state_view(state), nullptr);
    };

    type["addComponent"] = [](const LuaHandle<T> &self, const std::string &className, sol::this_state state) {
        SceneObject *object = resolveHandle(self, state);
        if (className == ScriptComponent::staticType().name) {
            luaL_error(state, "a script cannot attach a script");
        }

        for (const auto &component : object->components()) {
            if (instanceIsA(*component, className)) {
                return LuaHandleBase::push(sol::state_view(state), component.get());
            }
        }

        std::unique_ptr<SceneComponent> created = InstanceRegistry::createComponent(className, *object->scene(), className);
        if (created == nullptr) {
            luaL_error(state, "no component class is registered under that name");
        }

        SceneComponent *raw = created.get();
        object->attachComponent(std::move(created));
        raw->ready();
        return LuaHandleBase::push(sol::state_view(state), raw);
    };

    type["removeComponent"] = [](const LuaHandle<T> &self, const sol::object &value, sol::this_state state) {
        Instance *instance = resolveInstanceArgument(value, state);
        SceneComponent *component = instance->as<SceneComponent>();
        if (component == nullptr) {
            luaL_error(state, "expected a component");
        }
        if (component->isA<ScriptComponent>()) {
            luaL_error(state, "a script cannot detach a script");
        }
        resolveHandle(self, state)->removeComponent(component);
    };
}

template <typename T>
void addNode3DMembers(sol::usertype<LuaHandle<T>> &type)
{
    addSceneObjectMembers(type);

    type["position"] =
        sol::property([](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->position(); },
                      [](LuaHandle<T> &self, const glm::vec3 &position, sol::this_state state) {
                          resolveHandle(self, state)->setPosition(position);
                      });

    type["rotationE"] =
        sol::property([](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->rotation(); },
                      [](LuaHandle<T> &self, const glm::vec3 &rotation, sol::this_state state) {
                          resolveHandle(self, state)->setRotation(rotation);
                      });

    type["rotationQ"] =
        sol::property([](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->rotationQuat(); },
                      [](LuaHandle<T> &self, const glm::quat &rotation, sol::this_state state) {
                          resolveHandle(self, state)->setRotation(rotation);
                      });

    type["scale"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->scale(); },
        [](LuaHandle<T> &self, const glm::vec3 &scale, sol::this_state state) { resolveHandle(self, state)->setScale(scale); });

    type["worldPosition"] =
        sol::property([](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->worldPosition(); });

    type["forward"] = [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->forward(); };

    type["right"] = [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->right(); };

    type["up"] = [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->up(); };

    type["lookAt"] = [](const LuaHandle<T> &self, const glm::vec3 &target, sol::this_state state) {
        resolveHandle(self, state)->lookAt(target);
    };
}

template <typename T>
void addSceneComponentMembers(sol::usertype<LuaHandle<T>> &type)
{
    addInstanceMembers(type);

    type["owner"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), resolveHandle(self, state)->owner());
    });
}

template <typename T>
void addControllerMembers(sol::usertype<LuaHandle<T>> &type)
{
    addSceneObjectMembers(type);

    type["possessed"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), resolveHandle(self, state)->possessed());
    });

    type["capturesCursor"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->capturesCursor(); },
        [](LuaHandle<T> &self, bool captures, sol::this_state state) { resolveHandle(self, state)->setCapturesCursor(captures); });

    type["intent"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        const ControlInput &intent = resolveHandle(self, state)->intent();
        sol::state_view lua(state);
        return lua.create_table_with("look", intent.look, "move", intent.move, "zoom", intent.zoom, "jump", intent.jump, "orbit",
                                     intent.orbit, "pan", intent.pan);
    });

    type["addYawInput"] = [](const LuaHandle<T> &self, float degrees, sol::this_state state) {
        resolveHandle(self, state)->addYawInput(degrees);
    };

    type["addPitchInput"] = [](const LuaHandle<T> &self, float degrees, sol::this_state state) {
        resolveHandle(self, state)->addPitchInput(degrees);
    };

    type["controlRotation"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->controlRotation(); });

    type["controlForward"] =
        sol::property([](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->controlForward(); });

    type["controlRight"] =
        sol::property([](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->controlRight(); });

    type["maxPitch"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->maxPitch(); },
        [](LuaHandle<T> &self, float degrees, sol::this_state state) { resolveHandle(self, state)->setMaxPitch(degrees); });

    type["onPossess"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        Controller *controller = resolveHandle(self, state);
        return makeLuaEventSignal(controller, controller->onPossessionChanged);
    });

    type["onUpdate"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        Controller *controller = resolveHandle(self, state);
        return makeLuaEventSignal(controller, controller->onUpdateEvent);
    });
}

template <typename T>
void addPhysicsBody3DMembers(sol::usertype<LuaHandle<T>> &type)
{
    addSceneComponentMembers(type);

    type["velocity"] =
        sol::property([](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->velocity(); },
                      [](LuaHandle<T> &self, const glm::vec3 &velocity, sol::this_state state) {
                          resolveHandle(self, state)->setVelocity(velocity);
                      });
}

template <typename T>
void addCharacterBody3DMembers(sol::usertype<LuaHandle<T>> &type)
{
    addPhysicsBody3DMembers(type);

    type["walkSpeed"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->walkSpeed(); },
        [](LuaHandle<T> &self, float walkSpeed, sol::this_state state) { resolveHandle(self, state)->setWalkSpeed(walkSpeed); });

    type["move"] = [](const LuaHandle<T> &self, const glm::vec3 &direction, sol::this_state state) {
        resolveHandle(self, state)->move(direction);
    };

    type["jump"] = [](const LuaHandle<T> &self, sol::this_state state) { resolveHandle(self, state)->jump(); };

    type["isOnGround"] = [](const LuaHandle<T> &self, sol::this_state state) { return resolveHandle(self, state)->isOnGround(); };

    type["teleport"] = [](const LuaHandle<T> &self, const glm::vec3 &position, const glm::quat &rotation, sol::this_state state) {
        resolveHandle(self, state)->teleport(position, rotation);
    };
}
} // namespace Rapture::scripting

#endif // RAPTURE__LUAINSTANCEMEMBERS_H
