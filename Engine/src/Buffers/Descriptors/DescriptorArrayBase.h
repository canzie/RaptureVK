#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

namespace Rapture {

template<typename T>
class DescriptorSubAllocationBase;

struct DescriptorArrayConfig;

template<typename T>
class DescriptorArrayBase {
public:
    DescriptorArrayBase(const DescriptorArrayConfig& config, VkDescriptorSet set);
    virtual ~DescriptorArrayBase();

    // Not copyable or movable
    DescriptorArrayBase(const DescriptorArrayBase&) = delete;
    DescriptorArrayBase& operator=(const DescriptorArrayBase&) = delete;

    virtual std::unique_ptr<DescriptorSubAllocationBase<T>> createSubAllocation(uint32_t capacity, std::string name = "") = 0;
    virtual uint32_t allocate(std::shared_ptr<T> resource) = 0;
    virtual void update(uint32_t index, std::shared_ptr<T> resource) = 0;
    virtual void free(uint32_t index) = 0;
    
    // Getters for binding the resources during rendering.
    VkDescriptorSet getSet() const { return m_set; }
    uint32_t getCapacity() const { return m_capacity; }

protected:
    virtual void initializeSlotsWithDefault() = 0;
    virtual std::shared_ptr<T> createDefaultResource() = 0;

    VkDevice m_device;
    VkDescriptorType m_type;
    uint32_t m_capacity; 
    std::string m_name;

    VkDescriptorSet m_set = VK_NULL_HANDLE;

    // A simple mechanism to track available slots.
    std::vector<bool> m_isIndexUsed;
    uint32_t m_nextFreeIndex = 0;

    uint32_t m_bindingIndex;
    std::shared_ptr<T> m_defaultResource;
};

} // namespace Rapture