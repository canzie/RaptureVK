#include "BindlessDescriptorManager.h"
#include "Logging/Log.h"

namespace Rapture {

std::unordered_map<VkDescriptorType, std::shared_ptr<BindlessDescriptorArray>> BindlessDescriptorManager::m_BDAPools;

void BindlessDescriptorManager::init(std::vector<BindlessDescriptorArrayConfig> configs){ 
    RP_CORE_INFO("Initializing bindless descriptor manager");
    m_BDAPools.clear();
    
    for (const auto& config : configs) {
        m_BDAPools[config.type] = std::make_shared<BindlessDescriptorArray>(config);
    }
}

void BindlessDescriptorManager::shutdown() {
    m_BDAPools.clear();
}


std::shared_ptr<BindlessDescriptorArray> BindlessDescriptorManager::getPool(VkDescriptorType type) {
    auto it = m_BDAPools.find(type);
    if (it != m_BDAPools.end()) {
        return it->second;
    }
    RP_CORE_WARN("Attempted to get a bindless descriptor pool for a type that was not initialized: {}", static_cast<int>(type));
    return nullptr;
}

} // namespace Rapture 