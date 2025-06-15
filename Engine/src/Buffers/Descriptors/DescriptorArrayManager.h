#pragma once

#include "DescriptorArrayTypes.h"
#include "Buffers/Descriptors/DescriptorArrays/TextureDescriptorArray.h"
#include "Buffers/Descriptors/DescriptorArrays/StorageDescriptorArray.h"

#include <unordered_map>
#include <memory>
#include <variant>

namespace Rapture {

class DescriptorArrayManager {
public:
    static void init(std::vector<DescriptorArrayConfig> configs);
    static void shutdown();

    // Get a texture descriptor array
    static std::shared_ptr<TextureDescriptorArray> getTextureArray();
    
    // Get a storage/uniform buffer descriptor array
    static std::shared_ptr<StorageDescriptorArray> getStorageArray(DescriptorArrayType type);
    
    // Create sub-allocations
    static std::unique_ptr<DescriptorSubAllocationBase<Texture>> createTextureSubAllocation(uint32_t capacity, std::string name = "");
    static std::unique_ptr<DescriptorSubAllocationBase<Buffer>> createStorageSubAllocation(DescriptorArrayType type, uint32_t capacity, std::string name = "");

    // Unified descriptor set access
    static VkDescriptorSetLayout getUnifiedLayout() { return m_unifiedLayout; }
    static VkDescriptorSet getUnifiedSet() { return m_unifiedSet; }
    static VkDescriptorPool getDescriptorPool() { return m_descriptorPool; }

private:
    static void createDescriptorPools();
    static void createUnifiedDescriptorSet(const std::vector<DescriptorArrayConfig>& configs);

private:
    // Unified descriptor set management
    static VkDescriptorSetLayout m_unifiedLayout;
    static VkDescriptorSet m_unifiedSet;
    static VkDescriptorPool m_descriptorPool;

    // Individual arrays (no longer manage their own sets)
    static std::shared_ptr<TextureDescriptorArray> m_textureArray;
    static std::unordered_map<DescriptorArrayType, std::shared_ptr<StorageDescriptorArray>> m_storageArrays;
};

} // namespace Rapture 