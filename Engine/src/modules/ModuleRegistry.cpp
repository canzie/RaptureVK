#include "ModuleRegistry.h"

#include "logging/Log.h"
#include "modules/controllers/CameraController.h"
#include "modules/controllers/PlayerController.h"
#include "modules/puppets/Puppet.h"

#include <unordered_map>

namespace Rapture {

struct ModuleClassEntry {
    ModuleFactory factory;
    const TypeInfo *type;
};

static std::unordered_map<std::string_view, ModuleClassEntry> s_classes;
static bool s_isInitialized = false;

void ModuleRegistry::init()
{
    if (s_isInitialized) {
        RP_CORE_WARN("ModuleRegistry already initialized");
        return;
    }

    add<CameraController>();
    add<PlayerController>();
    add<Puppet>();

    s_isInitialized = true;
}

void ModuleRegistry::shutdown()
{
    s_classes.clear();
    s_isInitialized = false;
}

void ModuleRegistry::addFactory(const TypeInfo &type, ModuleFactory factory)
{
    auto [it, inserted] = s_classes.emplace(type.name, ModuleClassEntry{factory, &type});
    if (!inserted) {
        RP_CORE_WARN("'{}' is already registered, keeping the first class registered under it", type.name);
    }
}

std::unique_ptr<ModuleClass> ModuleRegistry::create(std::string_view className)
{
    auto it = s_classes.find(className);
    if (it == s_classes.end()) {
        return nullptr;
    }

    return it->second.factory();
}

const TypeInfo *ModuleRegistry::find(std::string_view className)
{
    auto it = s_classes.find(className);
    if (it == s_classes.end()) {
        return nullptr;
    }

    return it->second.type;
}

bool ModuleRegistry::contains(std::string_view className)
{
    return s_classes.find(className) != s_classes.end();
}

} // namespace Rapture
