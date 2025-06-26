// allocate pools for ibo/vbo to use, we will place them in the same buffer for locality
// when drawing a list of meshes we will now just need a prepass to sort them based on the buffers used.
// we will copy the design of the opengl version i made exepct reuse the same buffer for index and vertex data
// 




#pragma once

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


// overview of the design and flow


namespace Rapture {

// Forward declarations
class Buffer;
class VertexBuffer;
class IndexBuffer;

// Constants for buffer pool configuration
constexpr VkDeviceSize MEGA_BYTE = 1024 * 1024;
constexpr VkDeviceSize DEFAULT_ARENA_SIZE = 64 * MEGA_BYTE; // 64 MB default arena size
constexpr VkDeviceSize MAX_ARENA_SIZE = 256 * MEGA_BYTE;    // 256 MB maximum arena size

// Buffer type for allocation requests
enum class BufferType {
    VERTEX,
    INDEX
};

// Extended buffer usage flags
struct BufferFlags {
    bool useShaderDeviceAddress = true;  // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    bool useStorageBuffer = true;        // VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    bool useAccelerationStructure = false; // VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
};

// Metadata for tracking element offsets within allocations
struct AllocationElementInfo {
    BufferType type = BufferType::VERTEX;
    uint32_t elementSize = 0;       // Size of each element (vertex stride or index size)
    uint32_t elementCount = 0;      // Number of elements in this allocation
    uint32_t firstElementIndex = 0; // First element index within the arena (not byte offset)
};

// Parameters for requesting a buffer allocation
struct BufferAllocationRequest {
    VkDeviceSize size = 0;
    BufferType type = BufferType::VERTEX;
    BufferUsage usage = BufferUsage::STATIC;
    BufferFlags flags = BufferFlags();
    VkDeviceSize alignment = 1; // Default alignment
    BufferLayout layout; // Required for vertex buffers, can be provided for index buffers for grouping
    uint32_t indexSize = 4; // Size of index (4 for uint32_t, 2 for uint16_t) - only used for INDEX type

};

// Represents a single large buffer arena from which sub-allocations are made.
// This is managed by the BufferPoolManager.
struct BufferArena;

// Represents a sub-allocation within a BufferArena.
// A VertexBuffer or IndexBuffer will hold one of these.
struct BufferAllocation {
    std::shared_ptr<BufferArena> parentArena;
    VmaVirtualAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize offsetBytes = 0;
    VkDeviceSize sizeBytes = 0;
    AllocationElementInfo elementInfo; // Metadata for element offset calculations

    BufferAllocation() = default;
    ~BufferAllocation();

    bool isValid() const { return parentArena && allocation != VK_NULL_HANDLE; }
    
    // Get the actual VkBuffer handle
    VkBuffer getBuffer() const;
    
    // Get the device address of this allocation
    VkDeviceAddress getDeviceAddress() const;
    
    // Get the first element index (vertex/index number, not byte offset)
    uint32_t getFirstElementIndex() const { return elementInfo.firstElementIndex; }
    
    // Get the number of elements in this allocation
    uint32_t getElementCount() const { return elementInfo.elementCount; }
    
    // Get the size of each element
    uint32_t getElementSize() const { return elementInfo.elementSize; }
    
    // Upload data to this allocation
    void uploadData(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
};




// Definition of the BufferArena
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
    
    // Element tracking for mixed vertex/index buffer support
    uint32_t nextElementIndex = 0; // Next available element index in the arena
    std::vector<AllocationElementInfo> allocations; // Track all allocations for element calculations

    BufferArena(uint32_t id, VmaAllocator allocator, VkDeviceSize arenaSize, BufferUsage usage, 
                VkBufferUsageFlags usageFlags, const BufferFlags& flags);
    ~BufferArena();

    // Tries to allocate a block of memory from the virtual block.
    bool allocate( BufferAllocationRequest& request, BufferAllocation& outAllocation);
    void free(BufferAllocation& allocation);
    
    // Check if this arena is compatible with the given request
    bool isCompatible(const BufferAllocationRequest& request) const;
    
    // Get available space in this arena
    VkDeviceSize getAvailableSpace() const;
    
private:
    // Calculate element alignment based on request type
    VkDeviceSize calculateElementAlignment( BufferAllocationRequest& request);
    
    // Calculate element size for the request
    uint32_t calculateElementSize( BufferAllocationRequest& request) ;
    
    // Find the allocation info by VmaVirtualAllocation handle
    AllocationElementInfo* findAllocationInfo(VmaVirtualAllocation allocation);
};

// Manager class for all buffer arenas. This is a singleton.
class BufferPoolManager {
public:
    static BufferPoolManager& getInstance();
    
    static void init(VmaAllocator allocator);
    static void shutdown();

    // Allocate a single buffer (vertex or index)
    std::shared_ptr<BufferAllocation> allocateBuffer( BufferAllocationRequest& request);
    
    // Free a buffer allocation
    void freeBuffer(std::shared_ptr<BufferAllocation> allocation);
    

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


    
    // Layout-specific arena organization for vertex buffers
    // Key is the hash of the BufferLayout, value is list of arenas for that layout
    std::unordered_map<size_t, std::vector<std::shared_ptr<BufferArena>>> m_layoutToArenaMap;

    
    uint32_t m_nextArenaID = 0;
    
    static std::unique_ptr<BufferPoolManager> s_instance;
};
}





