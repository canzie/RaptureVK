#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

namespace Rapture {

class Texture;
class BindlessDescriptorArray;

struct BindlessDescriptorArrayConfig {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    uint32_t capacity = 0;
    std::string name="";
    uint32_t setBindingIndex = 0;
    uint32_t bindingIndex = 0;
};

// Represents a sub-allocation from a larger BindlessDescriptorArray.
// This allows a portion of a large descriptor array to be managed independently,
// for example, by a specific renderer or system.
class BindlessDescriptorSubAllocation {

public:
    // This should only be created by BindlessDescriptorArray.

    BindlessDescriptorSubAllocation(BindlessDescriptorArray* parent, uint32_t startIndex, uint32_t capacity, std::string name="");
    ~BindlessDescriptorSubAllocation();

    // Not copyable or movable.
    BindlessDescriptorSubAllocation(const BindlessDescriptorSubAllocation&) = delete;
    BindlessDescriptorSubAllocation& operator=(const BindlessDescriptorSubAllocation&) = delete;

    /**
     * @brief Allocates a descriptor from this sub-allocation's range.
     * @param texture The texture to bind.
     * @return The absolute bindless handle (index). Returns UINT32_MAX if full.
     */
    uint32_t allocate(std::shared_ptr<Texture> texture);

    /**
     * @brief Updates a descriptor at a specific absolute index within this sub-allocation's range.
     * @param index The absolute bindless handle to update.
     * @param texture The new texture resource.
     */
    void update(uint32_t index, std::shared_ptr<Texture> texture);

    /**
     * @brief Frees a descriptor at a specific absolute index, making it available again within this sub-allocation.
     * @param index The absolute bindless handle to free.
     */
    void free(uint32_t index);
    
    uint32_t getCapacity() const { return m_capacity; }
    uint32_t getStartIndex() const { return m_startIndex; }

    VkDescriptorSet getDescriptorSet() const;

private:
    BindlessDescriptorArray* m_parent;
    uint32_t m_startIndex;
    uint32_t m_capacity;
    uint32_t m_freeCount;

    std::string m_name;

    std::vector<bool> m_isIndexUsed;
    uint32_t m_nextFreeIndex = 0;
};

// Manages a single, large, bindless-style descriptor set array.
class BindlessDescriptorArray {
public:
    // Creates the pool, layout, and set for a bindless array of a specific type.
    BindlessDescriptorArray(BindlessDescriptorArrayConfig config);
    ~BindlessDescriptorArray();

    // Not copyable or movable to simplify resource ownership.
    BindlessDescriptorArray(const BindlessDescriptorArray&) = delete;
    BindlessDescriptorArray& operator=(const BindlessDescriptorArray&) = delete;

    /**
     * @brief Creates a sub-allocator that manages a contiguous block of descriptors from this array.
     * @param capacity The number of descriptors for the sub-allocation.
     * @return A unique_ptr to the sub-allocation, or nullptr if a block of the requested size cannot be found.
     */
    std::unique_ptr<BindlessDescriptorSubAllocation> createSubAllocation(uint32_t capacity, std::string name="");

    /**
     * @brief Finds the next available index, updates the descriptor to point to the texture,
     * and returns the index as a handle. For single, non-sub-allocated descriptors.
     * @param texture The texture resource to bind.
     * @return The bindless handle (index) for the texture.
     */
    uint32_t allocateSingle(std::shared_ptr<Texture> texture);

    /**
     * @brief Updates the descriptor at a specific index to point to a new texture.
     * @param index The bindless handle to update.
     * @param texture The new texture resource.
     */
    void update(uint32_t index, std::shared_ptr<Texture> texture);

    /**
     * @brief Frees an index, making it available for future allocations.
     * @param index The bindless handle to free.
     */
    void free(uint32_t index);
    
    // Getters for binding the resources during rendering.
    VkDescriptorSetLayout getLayout() const { return m_layout; }
    VkDescriptorSet getSet() const { return m_set; }
    uint32_t getCapacity() const { return m_capacity; }
    std::shared_ptr<Texture> getDefaultTexture() const { return m_defaultTexture; }

private:
    void createPool();
    void createLayout();
    void allocateSet();
    void initializeSlotsWithDefault(); // Good practice to avoid unbound descriptors

    VkDevice m_device;
    VkDescriptorType m_type;
    uint32_t m_capacity; 

    std::string m_name;

    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;

    // A simple mechanism to track available slots.
    std::vector<bool> m_isIndexUsed;
    uint32_t m_nextFreeIndex = 0;

    uint32_t m_setBindingIndex;
    uint32_t m_bindingIndex;

    std::shared_ptr<Texture> m_defaultTexture;
};

} // namespace Rapture
