#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

namespace Rapture {

template<typename T>
class DescriptorArrayBase;

template<typename T>
class DescriptorSubAllocationBase {
public:
    DescriptorSubAllocationBase(DescriptorArrayBase<T>* parent, uint32_t startIndex, uint32_t capacity, std::string name = "")
        : m_parent(parent), m_startIndex(startIndex), m_capacity(capacity), m_name(name), m_freeCount(capacity) {
        m_isIndexUsed.resize(capacity, false);
    }

    virtual ~DescriptorSubAllocationBase() = default;

    // Not copyable or movable
    DescriptorSubAllocationBase(const DescriptorSubAllocationBase&) = delete;
    DescriptorSubAllocationBase& operator=(const DescriptorSubAllocationBase&) = delete;

    /**
     * @brief Allocates a descriptor from this sub-allocation's range.
     * @param resource The resource to bind.
     * @return The absolute bindless handle (index). Returns UINT32_MAX if full.
     */
    uint32_t allocate(std::shared_ptr<T> resource) {
        for (uint32_t i = 0; i < m_capacity; ++i) {
            uint32_t relativeIndex = (m_nextFreeIndex + i) % m_capacity;
            if (!m_isIndexUsed[relativeIndex]) {
                m_isIndexUsed[relativeIndex] = true;
                m_nextFreeIndex = (relativeIndex + 1) % m_capacity;

                uint32_t absoluteIndex = m_startIndex + relativeIndex;
                m_parent->update(absoluteIndex, resource);
                m_freeCount--;
                return absoluteIndex;
            }
        }
        return UINT32_MAX; // Full
    }

    /**
     * @brief Updates a descriptor at a specific absolute index within this sub-allocation's range.
     * @param index The absolute bindless handle to update.
     * @param resource The new resource.
     */
    void update(uint32_t index, std::shared_ptr<T> resource) {
        if (index < m_startIndex || index >= (m_startIndex + m_capacity)) {
            return; // Out of range
        }
        m_parent->update(index, resource);
    }

    /**
     * @brief Frees a descriptor at a specific absolute index, making it available again within this sub-allocation.
     * @param index The absolute bindless handle to free.
     */
    void free(uint32_t index) {
        if (index < m_startIndex || index >= (m_startIndex + m_capacity)) {
            return; // Out of range
        }
        
        uint32_t relativeIndex = index - m_startIndex;
        if (m_isIndexUsed[relativeIndex]) {
            m_isIndexUsed[relativeIndex] = false;
            m_nextFreeIndex = relativeIndex;
            m_parent->free(index);
            m_freeCount++;
        }
    }
    
    uint32_t getCapacity() const { return m_capacity; }
    uint32_t getStartIndex() const { return m_startIndex; }
    uint32_t getFreeCount() const { return m_freeCount; }

    VkDescriptorSet getDescriptorSet() const {
        return m_parent->getSet();
    }

protected:
    DescriptorArrayBase<T>* m_parent;
    uint32_t m_startIndex;
    uint32_t m_capacity;
    uint32_t m_freeCount;
    std::string m_name;

    std::vector<bool> m_isIndexUsed;
    uint32_t m_nextFreeIndex = 0;
};

} // namespace Rapture
