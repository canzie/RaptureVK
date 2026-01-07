#ifndef RAPTURE__COMMAND_POOL_H
#define RAPTURE__COMMAND_POOL_H

#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rapture {

class CommandBuffer;
struct CmdBufferDefferedDestruction;
enum class CmdBufferLevel;

using CommandPoolHash = uint32_t;

static inline void hash_combine(size_t &seed, size_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

struct CommandPoolConfig {
    std::string name = "CommandPool";
    size_t threadId = 0;
    uint32_t queueFamilyIndex;
    VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPoolResetFlags resetFlags = 0;

    CommandPoolHash hash() const
    {
        size_t seed = 0;
        hash_combine(seed, std::hash<size_t>{}(threadId));
        hash_combine(seed, std::hash<uint32_t>{}(queueFamilyIndex));
        hash_combine(seed, std::hash<VkCommandPoolCreateFlags>{}(flags));
        hash_combine(seed, std::hash<VkCommandPoolResetFlags>{}(resetFlags));
        return static_cast<CommandPoolHash>(seed);
    }
};

// should automatically create a command pool for each queue family in a thread,
// not for each call but use a manager system to create a new pool when the conditions are met
// could also allow for manual specs.
class CommandPool : public std::enable_shared_from_this<CommandPool> {
  public:
    CommandPool(const CommandPoolConfig &config);
    ~CommandPool();

    VkCommandPool getCommandPoolVk() const { return m_commandPool; }
    CommandBuffer *getPrimaryCommandBuffer();
    CommandBuffer *getSecondaryCommandBuffer();

    void markPendingSignal(VkSemaphore timelineSemaphore, uint64_t signalValue);
    void resetIfNeeded();

  private:
    void allocateCommandBuffer(CmdBufferLevel level);

  private:
    VkCommandPoolCreateInfo m_createInfo;
    VkCommandPool m_commandPool;
    CommandPoolHash m_hash;

    VkDevice m_device;

    VkCommandPoolResetFlags m_resetFlags;

    uint32_t m_primaryCommandBufferIndex = 0;
    uint32_t m_secondaryCommandBufferIndex = 0;
    std::vector<std::unique_ptr<CommandBuffer>> m_primaryCommandBuffers;
    std::vector<std::unique_ptr<CommandBuffer>> m_secondaryCommandBuffers;

    std::unordered_map<VkSemaphore, uint64_t> m_pendingSignals;
    bool m_needsReset = false;
};

class CommandPoolManager {
  public:
    static void init(uint32_t framesInFlight);
    static void shutdown();

    static CommandPoolHash createCommandPool(const CommandPoolConfig &config);
    // access a pool by its hash
    static CommandPool *getCommandPool(CommandPoolHash hash, uint32_t frameIndex);
    static CommandPool *getCommandPool(CommandPoolHash hash);
    // the strict flag will return the command pool closest to the config provided (most same values)
    // assuming atleast 1 pool exists, this function will return a pool, given the strict flag=false
    // static std::shared_ptr<CommandPool> getCommandPool(const CommandPoolConfig& config, bool isStrict = true);

    static void beginFrame();
    static void endFrame();
    static void closeAllPools();

  private:
    static std::unordered_map<CommandPoolHash, std::vector<std::unique_ptr<CommandPool>>> s_commandPools;
    static std::mutex s_mutex;
    static uint32_t s_framesInFlight;
    static uint32_t s_currentFrameIndex;
};

} // namespace Rapture

#endif // RAPTURE__COMMAND_POOL_H