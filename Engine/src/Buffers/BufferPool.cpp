#include "BufferPool.h"

#include "Logging/Log.h"
#include "Utils/GLTypes.h"
#include "CommandBuffers/CommandPool.h"
#include "CommandBuffers/CommandBuffer.h"
#include "WindowContext/Application.h"

#include <cstring>
#include <functional>
#include <algorithm>

namespace Rapture {


std::unique_ptr<BufferPoolManager> BufferPoolManager::s_instance = nullptr;

//---------------------------------//
// BufferAllocation implementation //
//---------------------------------//

BufferAllocation::~BufferAllocation() {
    if (isValid()) {
        parentArena->free(this);
    }
}


VkBuffer BufferAllocation::getBuffer() const {
    return parentArena ? parentArena->buffer : VK_NULL_HANDLE;
}


void BufferAllocation::uploadData(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!isValid() || !data) {
        RP_CORE_ERROR("BufferAllocation::uploadData - Invalid allocation or null data");
        return;
    }
    
    if (offset + size > sizeBytes) {
        RP_CORE_ERROR("BufferAllocation::uploadData - Upload size {} + offset {} exceeds allocation size {}", 
                      size, offset, sizeBytes);
        return;
    }
    
    VmaAllocator allocator = parentArena->vmaAllocator;
    VkBuffer targetBuffer = parentArena->buffer;
    VkDeviceSize targetOffset = offsetBytes + offset;
    
    // Check if we can directly map the memory (for CPU-accessible buffers)
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(allocator, parentArena->vmaAllocation, &allocInfo);
    

    // Create staging buffer for GPU-only memory
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = size;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingInfo;
    
    VkResult result = vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingAllocInfo, 
                                        &stagingBuffer, &stagingAllocation, &stagingInfo);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("BufferAllocation::uploadData - Failed to create staging buffer");
        return;
    }
    
    // Copy data to staging buffer
    std::memcpy(stagingInfo.pMappedData, data, size);
    
    // Get command buffer from CommandPool system
    auto& app = Application::getInstance();
    auto& vulkanContext = app.getVulkanContext();
    
    // Create command pool config for transfer operations
    CommandPoolConfig poolConfig;
    poolConfig.queueFamilyIndex = vulkanContext.getQueueFamilyIndices().graphicsFamily.value(); // Using graphics queue for transfer
    poolConfig.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    
    auto commandPool = CommandPoolManager::createCommandPool(poolConfig);
    if (!commandPool) {
        RP_CORE_ERROR("BufferAllocation::uploadData - Failed to create command pool");
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return;
    }
    
    auto commandBuffer = commandPool->getCommandBuffer();
    if (!commandBuffer) {
        RP_CORE_ERROR("BufferAllocation::uploadData - Failed to get command buffer");
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return;
    }
    
    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer->getCommandBufferVk(), &beginInfo);
    
    // Record copy command
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = targetOffset;
    copyRegion.size = size;
    
    vkCmdCopyBuffer(commandBuffer->getCommandBufferVk(), stagingBuffer, targetBuffer, 1, &copyRegion);
    
    // End command buffer
    commandBuffer->end();
    
    auto graphicsQueue = vulkanContext.getGraphicsQueue();
    graphicsQueue->submitQueue(commandBuffer);
    graphicsQueue->waitIdle();
    
    // Clean up staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

}

void BufferAllocation::free() {
    if (isValid()) {
        parentArena->free(this);
    }
}



//----------------------------//
// BufferArena implementation //
//----------------------------//


BufferArena::BufferArena(uint32_t id, VmaAllocator allocator, VkDeviceSize arenaSize, 
                        BufferUsage usage, VkBufferUsageFlags usageFlags, const BufferFlags& flags)
    : id(id), vmaAllocator(allocator), size(arenaSize), usage(usage), usageFlags(usageFlags), flags(flags) {
    
    // Create the VMA virtual block for sub-allocations
    VmaVirtualBlockCreateInfo blockCreateInfo{};
    blockCreateInfo.size = arenaSize;
    blockCreateInfo.flags = 0; // Could add VMA_VIRTUAL_BLOCK_CREATE_LINEAR_ALGORITHM_BIT for linear allocation
    
    VkResult result = vmaCreateVirtualBlock(&blockCreateInfo, &virtualBlock);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("BufferArena: Failed to create virtual block with size {} MB", arenaSize / MEGA_BYTE);
        return;
    }
    
    // Create the actual VkBuffer
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = arenaSize;
    bufferCreateInfo.usage = usageFlags;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocCreateInfo{};
    switch (usage) {
        case BufferUsage::STATIC:
            allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            break;
        case BufferUsage::DYNAMIC:
            allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            break;
        case BufferUsage::STREAM:
            allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case BufferUsage::STAGING:
            allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
    }
    
    result = vmaCreateBuffer(vmaAllocator, &bufferCreateInfo, &allocCreateInfo, &buffer, &vmaAllocation, nullptr);
    if (result != VK_SUCCESS) {
        RP_CORE_ERROR("BufferArena: Failed to create buffer with size {} MB", arenaSize / MEGA_BYTE);
        vmaDestroyVirtualBlock(virtualBlock);
        virtualBlock = VK_NULL_HANDLE;
        return;
    }
    
    RP_CORE_INFO("BufferArena: Created arena {} with size {} MB", id, arenaSize / MEGA_BYTE);
}

BufferArena::~BufferArena() {
    clear();
}

bool BufferArena::allocate(VkDeviceSize size, VkDeviceSize alignment, BufferAllocation* outAllocation) {
    std::lock_guard<std::mutex> lock(mutex);
    
    VmaVirtualAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.size = size;
    allocCreateInfo.alignment = alignment;
    
    VmaVirtualAllocation allocation;
    VkDeviceSize offset;
    
    VkResult result = vmaVirtualAllocate(virtualBlock, &allocCreateInfo, &allocation, &offset);
    
    if (result == VK_SUCCESS && (alignment <= 1 || (offset % alignment) == 0)) {
        // The allocation worked and is properly aligned
        outAllocation->parentArena = shared_from_this();
        outAllocation->allocation = allocation;
        outAllocation->offsetBytes = offset;
        outAllocation->sizeBytes = size;
        return true;
    }
    
    // If that failed or gave us a misaligned result, free it and try manual alignment
    if (result == VK_SUCCESS) {
        vmaVirtualFree(virtualBlock, allocation);
    }
    
    // Calculate how much extra space we might need for alignment
    VkDeviceSize maxPadding = (alignment > 1) ? (alignment - 1) : 0;
    VkDeviceSize allocSize = size + maxPadding;
    
    // Try allocating with extra space for manual alignment
    allocCreateInfo.size = allocSize;
    allocCreateInfo.alignment = 1; // Use minimal alignment to avoid VMA conflicts
    
    result = vmaVirtualAllocate(virtualBlock, &allocCreateInfo, &allocation, &offset);
    if (result != VK_SUCCESS) {
        RP_CORE_TRACE("BufferArena {}: Failed to allocate {} bytes (including {} padding)", id, allocSize, maxPadding);
        return false;
    }
    
    // Manually calculate the aligned offset
    VkDeviceSize alignedOffset = offset;
    if (alignment > 1) {
        VkDeviceSize remainder = offset % alignment;
        if (remainder != 0) {
            alignedOffset = offset + (alignment - remainder);
        }
    }
    
    // Verify we have enough space for the aligned allocation
    if (alignedOffset + size > offset + allocSize) {
        RP_CORE_ERROR("BufferArena {}: Alignment calculation error - not enough space allocated", id);
        vmaVirtualFree(virtualBlock, allocation);
        return false;
    }
    
    outAllocation->parentArena = shared_from_this();
    outAllocation->allocation = allocation;
    outAllocation->offsetBytes = alignedOffset;
    outAllocation->sizeBytes = size;
    
    if (alignedOffset != offset) {
        RP_CORE_TRACE("BufferArena {}: Manual alignment: raw_offset={}, aligned_offset={}, padding={}", 
                     id, offset, alignedOffset, alignedOffset - offset);
    }
    
    return true;
}

void BufferArena::free(BufferAllocation* allocation) {
    if (!allocation->isValid() || allocation->parentArena.get() != this) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex);
    vmaVirtualFree(virtualBlock, allocation->allocation);
    
    // Clear the allocation data (but don't reset parentArena to avoid infinite recursion)
    allocation->allocation = VK_NULL_HANDLE;
    allocation->offsetBytes = 0;
    allocation->sizeBytes = 0;
}

void BufferArena::clear() {

    std::lock_guard<std::mutex> lock(mutex);

    if (!isValid()) {
        return;
    }

    vmaDestroyBuffer(vmaAllocator, buffer, vmaAllocation);
    vmaDestroyVirtualBlock(virtualBlock);
    
    RP_CORE_INFO("BufferArena: Destroyed arena {}", id);

}

bool BufferArena::isValid() const {
    return buffer != VK_NULL_HANDLE && virtualBlock != VK_NULL_HANDLE;
}

bool BufferArena::isCompatible(const BufferAllocationRequest &request) const
{

    if (usage != request.usage) {
        return false;
    }
    
    // Check buffer type compatibility (both vertex and index buffers can use the same arena)
    VkBufferUsageFlags requestFlags = 0;
    if (request.type == BufferType::VERTEX) {
        requestFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    } else if (request.type == BufferType::INDEX) {
        requestFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    
    // Check if this arena has the required buffer usage flags
    if (!(usageFlags & requestFlags)) {
        RP_CORE_ERROR("BufferArena::isCompatible - Arena {} does not have the required buffer usage flags", id);
        return false;
    }
    
    // Check additional flags compatibility
    if (request.flags.useShaderDeviceAddress && !(usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) {
        return false;
    }
    
    if (request.flags.useStorageBuffer && !(usageFlags & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return false;
    }
    
    if (request.flags.useAccelerationStructure && !(usageFlags & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)) {
        return false;
    }
    
    return true;
}

VkDeviceSize BufferArena::getAvailableSpace() const {
    if (virtualBlock == VK_NULL_HANDLE) {
        return 0;
    }
    
    VmaStatistics stats;
    vmaGetVirtualBlockStatistics(virtualBlock, &stats);
    return stats.allocationBytes > size ? 0 : size - stats.allocationBytes;
}

//----------------------------------//
// BufferPoolManager implementation //
//----------------------------------//

BufferPoolManager& BufferPoolManager::getInstance() {
    if (!s_instance) {
        s_instance = std::unique_ptr<BufferPoolManager>(new BufferPoolManager());
    }
    return *s_instance;
}

void BufferPoolManager::init(VmaAllocator allocator) {
    auto& instance = getInstance();
    std::lock_guard<std::mutex> lock(instance.m_mutex);
    instance.m_allocator = allocator;
    RP_CORE_INFO("BufferPoolManager: Initialized with VMA allocator");
}

void BufferPoolManager::shutdown() {
    if (s_instance) {
        std::lock_guard<std::mutex> lock(s_instance->m_mutex);
        s_instance->m_layoutToArenaMap.clear();
        s_instance->m_allocator = VK_NULL_HANDLE;
        RP_CORE_INFO("BufferPoolManager: Shutdown complete");
    } else {
        RP_CORE_ERROR("BufferPoolManager: Shutdown called but not initialized!");
    }
    s_instance.reset();
}

std::shared_ptr<BufferAllocation> BufferPoolManager::allocateBuffer(const BufferAllocationRequest& request) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_allocator == VK_NULL_HANDLE) {
        RP_CORE_ERROR("BufferPoolManager: Not initialized!");
        return nullptr;
    }
    
    auto arena = findOrCreateArena(request);
    if (!arena) {
        RP_CORE_ERROR("BufferPoolManager: Failed to find or create arena for buffer allocation");
        return nullptr;
    }
    
    auto allocation = std::make_shared<BufferAllocation>();
    if (!arena->allocate(request.size, request.alignment, allocation.get())) {
        RP_CORE_ERROR("BufferPoolManager: Failed to allocate {} bytes from arena {}", request.size, arena->id);
        return nullptr;
    }
    

    return allocation;
}

void BufferPoolManager::freeBuffer(std::shared_ptr<BufferArena> arena) {
    if (!arena) {
        return;
    }


    for (auto map_it = m_layoutToArenaMap.begin(); map_it != m_layoutToArenaMap.end(); ++map_it) {
        auto& arenas = map_it->second;
        
        auto it = std::find(arenas.begin(), arenas.end(), arena);

        if (it != arenas.end()) {
            arenas.erase(it);

            if (arenas.empty()) {
                arena->clear(); // this will make the arena invalid
                m_layoutToArenaMap.erase(map_it);
            }
            return;
        }
    }
}


std::shared_ptr<BufferArena> BufferPoolManager::findOrCreateArena(const BufferAllocationRequest& request) {

    // For vertex buffers, look in layout-specific arenas
    size_t layoutHash = request.layout.hash();
    auto it = m_layoutToArenaMap.find(layoutHash);
    
    if (it != m_layoutToArenaMap.end()) {
        // Check existing arenas for this layout
        for (auto& arena : it->second) {
            if (arena->isCompatible(request) && arena->getAvailableSpace() >= request.size) {
                return arena;
            }
        }
    }


    return createArena(request);
    

}

std::shared_ptr<BufferArena> BufferPoolManager::createArena(const BufferAllocationRequest& request) {
    VkDeviceSize arenaSize = calculateArenaSize(request);
    VkBufferUsageFlags usageFlags = generateUsageFlags(request.type, request.flags);
    
    uint32_t arenaId = m_nextArenaID++;
    
    auto arena = std::make_shared<BufferArena>(arenaId, m_allocator, arenaSize, request.usage, usageFlags, request.flags);
    
    if (arena->buffer == VK_NULL_HANDLE) {
        RP_CORE_ERROR("BufferPoolManager: Failed to create arena {}", arenaId);
        return nullptr;
    }
    
    
        // Add to layout-specific collection
        size_t layoutHash = request.layout.hash();
        m_layoutToArenaMap[layoutHash].push_back(arena);

    
    return arena;
}


// uses a (too) simple heuristic to calculate the arena size
// most cases will use a 64mb arena because of this, this is fine an close to the
// bufferpool sizes of other engines
VkDeviceSize BufferPoolManager::calculateArenaSize(const BufferAllocationRequest& request) const {
    VkDeviceSize arenaSize = DEFAULT_ARENA_SIZE;
    
    if (request.size > DEFAULT_ARENA_SIZE / 2) {
        arenaSize = std::max(request.size * 2, DEFAULT_ARENA_SIZE);
    }
    
    arenaSize = std::min(arenaSize, MAX_ARENA_SIZE);
    
    arenaSize = std::max(arenaSize, request.size);
    
    return arenaSize;
}

VkBufferUsageFlags BufferPoolManager::generateUsageFlags(BufferType type, const BufferFlags& flags) const {
    VkBufferUsageFlags usageFlags = 0;
    
    (void)type;

    // Always include both vertex and index buffer bits for maximum compatibility
    usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    
    // Add transfer flags for staging operations
    usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    // Add optional flags
    if (flags.useShaderDeviceAddress) {
        usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    
    if (flags.useStorageBuffer) {
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    
    if (flags.useAccelerationStructure) {
        usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }
    
    return usageFlags;
}

} // namespace Rapture 