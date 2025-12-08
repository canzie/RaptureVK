/*
* allocate pools for ibo/vbo to use, we will place them in the same buffer for locality and mdi.
* when drawing a list of meshes we will now just need a prepass to sort them based on the buffers used.
*/
 


#ifndef RAPTURE__BUFFER_POOL_H
#define RAPTURE__BUFFER_POOL_H

#include "VertexBuffers/BufferLayout.h"
#include "Buffers.h"
#include "Logging/Log.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <vector>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <algorithm>



namespace Rapture {


// buffer pool configuration
constexpr VkDeviceSize MEGA_BYTE = 1024 * 1024;
constexpr VkDeviceSize DEFAULT_ARENA_SIZE = 64 * MEGA_BYTE; // 64 MB default arena size
constexpr VkDeviceSize MAX_ARENA_SIZE = 256 * MEGA_BYTE;    // 256 MB maximum arena size

// Buffer type for allocation requests
enum class BufferType {
    VERTEX,
    INDEX
};


struct BufferFlags {
    bool useShaderDeviceAddress = true;  // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    bool useStorageBuffer = true;        // VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    bool useAccelerationStructure = true; // VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
};

struct BufferAllocationRequest {
    VkDeviceSize size = 0;
    BufferType type = BufferType::VERTEX;
    BufferUsage usage = BufferUsage::STATIC;
    BufferFlags flags = BufferFlags();
    VkDeviceSize alignment = 1; // Default alignment (usually bufferlayout size)
    BufferLayout layout; // Required for vertex buffers
    uint32_t indexSize = 2; // 2 bytes for 16 bit indices, 4 bytes for 32 bit indices

};


struct BufferArena;

// Represents a sub-allocation within a BufferArena.
// A VertexBuffer or IndexBuffer will hold one of these.
struct BufferAllocation {
    std::shared_ptr<BufferArena> parentArena;
    VmaVirtualAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize offsetBytes = 0;
    VkDeviceSize sizeBytes = 0;

    BufferAllocation() = default;
    ~BufferAllocation();

    bool isValid() const { return parentArena && allocation != VK_NULL_HANDLE; }
    
    VkBuffer getBuffer() const;
    
    VkDeviceAddress getDeviceAddress() const;
    
    void uploadData(const void* data, VkDeviceSize size, VkDeviceSize offset=0);
    void free();
    
};





struct BufferArena : public std::enable_shared_from_this<BufferArena> {
    uint32_t id;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation vmaAllocation = VK_NULL_HANDLE;
    VmaVirtualBlock virtualBlock = VK_NULL_HANDLE;
    VmaAllocator vmaAllocator = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    BufferUsage usage;
    VkBufferUsageFlags usageFlags;
    BufferFlags flags;
    std::mutex mutex;

    BufferArena(uint32_t id, VmaAllocator allocator, VkDeviceSize arenaSize, BufferUsage usage, 
                VkBufferUsageFlags usageFlags, const BufferFlags& flags);
    ~BufferArena();

    bool allocate(VkDeviceSize size, VkDeviceSize alignment, BufferAllocation* outAllocation);
    void free(BufferAllocation* allocation);
    void clear();

    bool isValid() const;
    
    bool isCompatible(const BufferAllocationRequest& request) const;

    VkDeviceSize getAvailableSpace() const;
};


class BufferPoolManager {
public:
    static BufferPoolManager& getInstance();
    
    static void init(VmaAllocator allocator);
    static void shutdown();

    std::shared_ptr<BufferAllocation> allocateBuffer(const BufferAllocationRequest& request);
    
    // removes the arena from the pools map, there will still be a reference to the arena outside of the pool
    // but the arena will be invalidated and the memory will be freed, same for its allocations
    void freeBuffer(std::shared_ptr<BufferArena> arena);
    
    


private:
    BufferPoolManager() = default;

    // Finds an arena with enough space or creates a new one.
    std::shared_ptr<BufferArena> findOrCreateArena(const BufferAllocationRequest& request);
    
    // Create a new arena for the given request
    std::shared_ptr<BufferArena> createArena(const BufferAllocationRequest& request);
    
    // Calculate appropriate arena size based on request
    VkDeviceSize calculateArenaSize(const BufferAllocationRequest& request) const;
    
    // Generate usage flags for arena creation
    VkBufferUsageFlags generateUsageFlags(BufferType type, const BufferFlags& flags) const;
    
    
private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    std::mutex m_mutex;

    // Key is the hash of the BufferLayout, value is list of arenas for that layout
    std::unordered_map<size_t, std::vector<std::shared_ptr<BufferArena>>> m_layoutToArenaMap;

    uint32_t m_nextArenaID = 0;
    
    static std::unique_ptr<BufferPoolManager> s_instance;
};

}

#endif // RAPTURE__BUFFER_POOL_H