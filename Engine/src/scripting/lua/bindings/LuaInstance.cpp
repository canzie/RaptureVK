#include "LuaBindings.h"

#include "scene/instances/CharacterBody3D.h"
#include "scene/instances/InstanceRegistry.h"
#include "scene/instances/Node3D.h"
#include "scene/instances/SpringArm3D.h"
#include "scene/instances/controllers/Controller.h"
#include "scene/instances/scene_components/ScriptComponent.h"
#include "scene/Scene.h"
#include "scripting/lua/LuaEventSignal.h"
#include "scripting/lua/LuaHandle.h"

namespace Rapture::scripting {

template <typename T>
static T *s_resolve(const LuaHandle<T> &handle, lua_State *state)
{
    T *object = handle.get();
    if (object == nullptr) {
        luaL_error(state, "instance has been destroyed");
    }

    return object;
}

static Instance *s_resolveArgument(const sol::object &value, lua_State *state)
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

static bool s_isA(const Instance &instance, std::string_view className)
{
    const TypeInfo &type = instance.type();
    for (uint8_t i = 0; i <= type.depth; i++) {
        if (type.chain[i]->name == className) {
            return true;
        }
    }

    return false;
}

static SceneObject *s_findDescendantOfType(const SceneObject &object, std::string_view className)
{
    for (const auto &child : object.children()) {
        if (s_isA(*child, className)) {
            return child.get();
        }
        if (SceneObject *found = s_findDescendantOfType(*child, className)) {
            return found;
        }
    }

    return nullptr;
}

static bool s_isDescendantOf(const SceneObject &object, const SceneObject &ancestor)
{
    for (const SceneObject *walk = &object; walk != nullptr; walk = walk->parent()) {
        if (walk == &ancestor) {
            return true;
        }
    }

    return false;
}

static void s_reparent(SceneObject &object, SceneObject &parent, lua_State *state)
{
    if (&object == &parent || s_isDescendantOf(parent, object)) {
        luaL_error(state, "cannot parent an object below itself");
    }

    SceneObject *previous = object.parent();
    if (previous == nullptr) {
        luaL_error(state, "cannot reparent the scene root");
    }

    parent.addChild(previous->removeChild(&object));
}

static sol::table s_snapshot(sol::state_view lua, std::span<const std::unique_ptr<SceneObject>> objects)
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

static sol::table s_snapshot(sol::state_view lua, std::span<const std::unique_ptr<SceneComponent>> components)
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
static void s_addInstanceMembers(sol::usertype<LuaHandle<T>> &type)
{
    type["id"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return std::to_string(s_resolve(self, state)->id());
    });

    type["name"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) {
            return std::string(s_resolve(self, state)->name());
        },
        [](LuaHandle<T> &self, const std::string &name, sol::this_state state) {
            s_resolve(self, state)->setName(name);
        });

    type["isA"] = [](const LuaHandle<T> &self, const std::string &className, sol::this_state state) {
        return s_isA(*s_resolve(self, state), className);
    };

    type["onDestroy"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        Instance *instance = s_resolve(self, state);
        return makeLuaEventSignal(instance, instance->onDestroy);
    });
}

template <typename T>
static void s_addSceneObjectMembers(sol::usertype<LuaHandle<T>> &type)
{
    s_addInstanceMembers(type);

    type["parent"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) {
            return LuaHandleBase::push(sol::state_view(state), s_resolve(self, state)->parent());
        },
        [](LuaHandle<T> &self, const sol::object &value, sol::this_state state) {
            Instance *parent = s_resolveArgument(value, state);
            SceneObject *object = parent->as<SceneObject>();
            if (object == nullptr) {
                luaL_error(state, "a parent has to be a scene object");
            }
            s_reparent(*s_resolve(self, state), *object, state);
        });

    type["children"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return s_snapshot(sol::state_view(state), s_resolve(self, state)->children());
    });

    type["components"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return s_snapshot(sol::state_view(state), s_resolve(self, state)->components());
    });

    type["findChild"] = [](const LuaHandle<T> &self, const std::string &name, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), s_resolve(self, state)->findChild(name));
    };

    type["findDescendant"] = [](const LuaHandle<T> &self, const std::string &path, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), s_resolve(self, state)->findDescendant(path));
    };

    type["findFirstDescendantOfType"] = [](const LuaHandle<T> &self, const std::string &className,
                                           sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), s_findDescendantOfType(*s_resolve(self, state), className));
    };

    type["getComponent"] = [](const LuaHandle<T> &self, const std::string &className, sol::this_state state) {
        for (const auto &component : s_resolve(self, state)->components()) {
            if (s_isA(*component, className)) {
                return LuaHandleBase::push(sol::state_view(state), component.get());
            }
        }
        return LuaHandleBase::push(sol::state_view(state), nullptr);
    };

    type["addComponent"] = [](const LuaHandle<T> &self, const std::string &className, sol::this_state state) {
        SceneObject *object = s_resolve(self, state);
        if (className == ScriptComponent::staticType().name) {
            luaL_error(state, "a script cannot attach a script");
        }

        for (const auto &component : object->components()) {
            if (s_isA(*component, className)) {
                return LuaHandleBase::push(sol::state_view(state), component.get());
            }
        }

        std::unique_ptr<SceneComponent> created =
            InstanceRegistry::createComponent(className, *object->scene(), className);
        if (created == nullptr) {
            luaL_error(state, "no component class is registered under that name");
        }

        SceneComponent *raw = created.get();
        object->attachComponent(std::move(created));
        raw->ready();
        return LuaHandleBase::push(sol::state_view(state), raw);
    };

    type["removeComponent"] = [](const LuaHandle<T> &self, const sol::object &value, sol::this_state state) {
        Instance *instance = s_resolveArgument(value, state);
        SceneComponent *component = instance->as<SceneComponent>();
        if (component == nullptr) {
            luaL_error(state, "expected a component");
        }
        if (component->isA<ScriptComponent>()) {
            luaL_error(state, "a script cannot detach a script");
        }
        s_resolve(self, state)->removeComponent(component);
    };
}

template <typename T>
static void s_addNode3DMembers(sol::usertype<LuaHandle<T>> &type)
{
    s_addSceneObjectMembers(type);

    type["position"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->position(); },
        [](LuaHandle<T> &self, const glm::vec3 &position, sol::this_state state) {
            s_resolve(self, state)->setPosition(position);
        });

    type["rotationE"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->rotation(); },
        [](LuaHandle<T> &self, const glm::vec3 &rotation, sol::this_state state) {
            s_resolve(self, state)->setRotation(rotation);
        });

    type["rotationQ"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->rotationQuat(); },
        [](LuaHandle<T> &self, const glm::quat &rotation, sol::this_state state) {
            s_resolve(self, state)->setRotation(rotation);
        });

    type["scale"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->scale(); },
        [](LuaHandle<T> &self, const glm::vec3 &scale, sol::this_state state) {
            s_resolve(self, state)->setScale(scale);
        });

    type["worldPosition"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->worldPosition();
    });

    type["forward"] = [](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->forward();
    };

    type["right"] = [](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->right();
    };

    type["up"] = [](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->up();
    };

    type["lookAt"] = [](const LuaHandle<T> &self, const glm::vec3 &target, sol::this_state state) {
        s_resolve(self, state)->lookAt(target);
    };
}

template <typename T>
static void s_addSceneComponentMembers(sol::usertype<LuaHandle<T>> &type)
{
    s_addInstanceMembers(type);

    type["owner"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), s_resolve(self, state)->owner());
    });
}

template <typename T>
static void s_addControllerMembers(sol::usertype<LuaHandle<T>> &type)
{
    s_addSceneObjectMembers(type);

    type["possessed"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return LuaHandleBase::push(sol::state_view(state), s_resolve(self, state)->possessed());
    });

    type["capturesCursor"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->capturesCursor(); },
        [](LuaHandle<T> &self, bool captures, sol::this_state state) {
            s_resolve(self, state)->setCapturesCursor(captures);
        });

    type["intent"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        const ControlInput &intent = s_resolve(self, state)->intent();
        sol::state_view lua(state);
        return lua.create_table_with("look", intent.look, "move", intent.move, "zoom", intent.zoom, "jump",
                                     intent.jump, "orbit", intent.orbit, "pan", intent.pan);
    });

    type["addYawInput"] = [](const LuaHandle<T> &self, float degrees, sol::this_state state) {
        s_resolve(self, state)->addYawInput(degrees);
    };

    type["addPitchInput"] = [](const LuaHandle<T> &self, float degrees, sol::this_state state) {
        s_resolve(self, state)->addPitchInput(degrees);
    };

    type["controlRotation"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->controlRotation();
    });

    type["controlForward"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->controlForward();
    });

    type["controlRight"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->controlRight();
    });

    type["maxPitch"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->maxPitch(); },
        [](LuaHandle<T> &self, float degrees, sol::this_state state) {
            s_resolve(self, state)->setMaxPitch(degrees);
        });

    type["onPossess"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        Controller *controller = s_resolve(self, state);
        return makeLuaEventSignal(controller, controller->onPossessionChanged);
    });

    type["onUpdate"] = sol::property([](const LuaHandle<T> &self, sol::this_state state) {
        Controller *controller = s_resolve(self, state);
        return makeLuaEventSignal(controller, controller->onUpdateEvent);
    });
}

template <typename T>
static void s_addPhysicsBody3DMembers(sol::usertype<LuaHandle<T>> &type)
{
    s_addSceneComponentMembers(type);

    type["velocity"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->velocity(); },
        [](LuaHandle<T> &self, const glm::vec3 &velocity, sol::this_state state) {
            s_resolve(self, state)->setVelocity(velocity);
        });
}

template <typename T>
static void s_addCharacterBody3DMembers(sol::usertype<LuaHandle<T>> &type)
{
    s_addPhysicsBody3DMembers(type);

    type["walkSpeed"] = sol::property(
        [](const LuaHandle<T> &self, sol::this_state state) { return s_resolve(self, state)->walkSpeed(); },
        [](LuaHandle<T> &self, float walkSpeed, sol::this_state state) {
            s_resolve(self, state)->setWalkSpeed(walkSpeed);
        });

    type["move"] = [](const LuaHandle<T> &self, const glm::vec3 &direction, sol::this_state state) {
        s_resolve(self, state)->move(direction);
    };

    type["jump"] = [](const LuaHandle<T> &self, sol::this_state state) { s_resolve(self, state)->jump(); };

    type["isOnGround"] = [](const LuaHandle<T> &self, sol::this_state state) {
        return s_resolve(self, state)->isOnGround();
    };

    type["teleport"] = [](const LuaHandle<T> &self, const glm::vec3 &position, const glm::quat &rotation,
                          sol::this_state state) { s_resolve(self, state)->teleport(position, rotation); };
}

void registerInstanceBindings(sol::state_view lua)
{
    auto instance = lua.new_usertype<LuaHandle<Instance>>("Instance", sol::no_constructor, sol::base_classes,
                                                          sol::bases<LuaHandleBase>());
    s_addInstanceMembers(instance);
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
            parent = s_resolveArgument(*parentValue, state)->as<SceneObject>();
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
    s_addSceneObjectMembers(sceneObject);
    LuaHandleBase::registerType<SceneObject>();

    auto node3D = lua.new_usertype<LuaHandle<Node3D>>("Node3D", sol::no_constructor, sol::base_classes,
                                                      sol::bases<LuaHandleBase>());
    s_addNode3DMembers(node3D);
    LuaHandleBase::registerType<Node3D>();

    auto springArm = lua.new_usertype<LuaHandle<SpringArm3D>>(
        "SpringArm3D", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    s_addNode3DMembers(springArm);
    springArm["length"] = sol::property(
        [](const LuaHandle<SpringArm3D> &self, sol::this_state state) { return s_resolve(self, state)->length(); },
        [](LuaHandle<SpringArm3D> &self, float length, sol::this_state state) {
            s_resolve(self, state)->setLength(length);
        });
    springArm["followsControlRotation"] = sol::property(
        [](const LuaHandle<SpringArm3D> &self, sol::this_state state) {
            return s_resolve(self, state)->followsControlRotation();
        },
        [](LuaHandle<SpringArm3D> &self, bool follows, sol::this_state state) {
            s_resolve(self, state)->setFollowsControlRotation(follows);
        });
    LuaHandleBase::registerType<SpringArm3D>();

    auto controller = lua.new_usertype<LuaHandle<Controller>>(
        "Controller", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    s_addControllerMembers(controller);
    LuaHandleBase::registerType<Controller>();

    auto sceneComponent = lua.new_usertype<LuaHandle<SceneComponent>>(
        "SceneComponent", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    s_addSceneComponentMembers(sceneComponent);
    LuaHandleBase::registerType<SceneComponent>();

    auto scriptComponent = lua.new_usertype<LuaHandle<ScriptComponent>>(
        "ScriptComponent", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    s_addSceneComponentMembers(scriptComponent);
    LuaHandleBase::registerType<ScriptComponent>();

    auto characterBody = lua.new_usertype<LuaHandle<CharacterBody3D>>(
        "CharacterBody3D", sol::no_constructor, sol::base_classes, sol::bases<LuaHandleBase>());
    s_addCharacterBody3DMembers(characterBody);
    LuaHandleBase::registerType<CharacterBody3D>();
}

} // namespace Rapture::scripting
