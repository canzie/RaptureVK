#include "InstanceRegistry.h"

#include "logging/Log.h"
#include "scenes/instances/Camera3D.h"
#include "scenes/instances/CharacterBody3D.h"
#include "scenes/instances/DirectionalLight3D.h"
#include "scenes/instances/Environment.h"
#include "scenes/instances/Folder.h"
#include "scenes/instances/Node3D.h"
#include "scenes/instances/PointLight3D.h"
#include "scenes/instances/RigidBody3D.h"
#include "scenes/instances/SpotLight3D.h"
#include "scenes/instances/SpringArm3D.h"
#include "scenes/instances/StaticMesh3D.h"
#include "scenes/instances/Terrain3D.h"
#include "scenes/instances/controllers/CameraController.h"
#include "scenes/instances/controllers/PlayerController.h"
#include "scenes/instances/scene_components/VisibilityComponent.h"

#include <unordered_map>
#include <vector>

namespace Rapture {

static std::unordered_map<std::string_view, SceneObjectFactory> s_objectFactories;
static std::unordered_map<std::string_view, SceneComponentFactory> s_componentFactories;
static std::vector<const TypeInfo *> s_objectClasses;
static std::vector<const TypeInfo *> s_componentClasses;
static bool s_isInitialized = false;

void InstanceRegistry::init()
{
    if (s_isInitialized) {
        RP_CORE_WARN("InstanceRegistry already initialized");
        return;
    }

    addObject<Folder>();
    addObject<Node3D>();
    addObject<Camera3D>();
    addObject<SpringArm3D>();
    addObject<StaticMesh3D>();
    addObject<Terrain3D>();
    addObject<DirectionalLight3D>();
    addObject<PointLight3D>();
    addObject<SpotLight3D>();
    addObject<Environment>();
    addObject<CameraController>();
    addObject<PlayerController>();

    addComponent<CharacterBody3D>();
    addComponent<RigidBody3D>();
    addComponent<VisibilityComponent>();

    s_isInitialized = true;
}

void InstanceRegistry::shutdown()
{
    s_objectFactories.clear();
    s_componentFactories.clear();
    s_objectClasses.clear();
    s_componentClasses.clear();
    s_isInitialized = false;
}

void InstanceRegistry::addObjectFactory(const TypeInfo &type, SceneObjectFactory factory)
{
    auto [it, inserted] = s_objectFactories.emplace(type.name, factory);
    if (!inserted) {
        RP_CORE_WARN("'{}' is already registered, keeping the first class registered under it", type.name);
        return;
    }

    s_objectClasses.push_back(&type);
}

void InstanceRegistry::addComponentFactory(const TypeInfo &type, SceneComponentFactory factory)
{
    auto [it, inserted] = s_componentFactories.emplace(type.name, factory);
    if (!inserted) {
        RP_CORE_WARN("'{}' is already registered, keeping the first class registered under it", type.name);
        return;
    }

    s_componentClasses.push_back(&type);
}

std::unique_ptr<SceneObject> InstanceRegistry::createObject(std::string_view className, Scene &scene, std::string_view name)
{
    auto it = s_objectFactories.find(className);
    if (it == s_objectFactories.end()) {
        return nullptr;
    }

    return it->second(scene, name);
}

std::unique_ptr<SceneComponent> InstanceRegistry::createComponent(std::string_view className, Scene &scene, std::string_view name)
{
    auto it = s_componentFactories.find(className);
    if (it == s_componentFactories.end()) {
        return nullptr;
    }

    return it->second(scene, name);
}

bool InstanceRegistry::containsObject(std::string_view className)
{
    return s_objectFactories.find(className) != s_objectFactories.end();
}

bool InstanceRegistry::containsComponent(std::string_view className)
{
    return s_componentFactories.find(className) != s_componentFactories.end();
}

std::span<const TypeInfo *const> InstanceRegistry::objectClasses()
{
    return s_objectClasses;
}

std::span<const TypeInfo *const> InstanceRegistry::componentClasses()
{
    return s_componentClasses;
}

const TypeInfo *InstanceRegistry::find(std::string_view className)
{
    for (const TypeInfo *type : s_objectClasses) {
        if (type->name == className) {
            return type;
        }
    }

    for (const TypeInfo *type : s_componentClasses) {
        if (type->name == className) {
            return type;
        }
    }

    return nullptr;
}

} // namespace Rapture
