#include "InstanceRegistry.h"

#include "logging/Log.h"
#include "scenes/instances/Camera3D.h"
#include "scenes/instances/DirectionalLight3D.h"
#include "scenes/instances/Environment.h"
#include "scenes/instances/Folder.h"
#include "scenes/instances/Node3D.h"
#include "scenes/instances/PointLight3D.h"
#include "scenes/instances/PrefabInstance.h"
#include "scenes/instances/SpotLight3D.h"
#include "scenes/instances/StaticMesh3D.h"
#include "scenes/instances/Terrain3D.h"

#include <unordered_map>

namespace Rapture {

static std::unordered_map<std::string_view, InstanceFactory> s_factories;
static bool s_isInitialized = false;

void InstanceRegistry::init()
{
    if (s_isInitialized) {
        RP_CORE_WARN("InstanceRegistry already initialized");
        return;
    }

    add<Folder>();
    add<Node3D>();
    add<Camera3D>();
    add<StaticMesh3D>();
    add<Terrain3D>();
    add<PrefabInstance>();
    add<DirectionalLight3D>();
    add<PointLight3D>();
    add<SpotLight3D>();
    add<Environment>();

    s_isInitialized = true;
}

void InstanceRegistry::shutdown()
{
    s_factories.clear();
    s_isInitialized = false;
}

void InstanceRegistry::addFactory(std::string_view className, InstanceFactory factory)
{
    auto [it, inserted] = s_factories.emplace(className, factory);
    if (!inserted) {
        RP_CORE_WARN("'{}' is already registered, keeping the first class registered under it", className);
    }
}

std::unique_ptr<Instance> InstanceRegistry::create(std::string_view className, Scene &scene, std::string_view name)
{
    auto it = s_factories.find(className);
    if (it == s_factories.end()) {
        return nullptr;
    }

    return it->second(scene, name);
}

bool InstanceRegistry::contains(std::string_view className)
{
    return s_factories.find(className) != s_factories.end();
}

} // namespace Rapture
