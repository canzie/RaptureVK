 #pragma once

#include "BindlessDescriptorArray.h"

#include <unordered_map>
#include <memory>

namespace Rapture {

// TODO: for now the application will not expand the BDA, this should change to allow for dynamic expansion.

class BindlessDescriptorManager {
public:

    static void init(std::vector<BindlessDescriptorArrayConfig> configs);
    static void shutdown();

    static std::shared_ptr<BindlessDescriptorArray> getPool(VkDescriptorType type);

private:
    static std::unordered_map<VkDescriptorType, std::shared_ptr<BindlessDescriptorArray>> m_BDAPools;

};

 }