#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

enum class CmdBufferState {
    INITIAL,
    RECORDING,
    EXECUTABLE,
    PENDING,
    INVALID
};

struct CmdBufferDefferedDestruction {
    VkCommandBuffer commandBuffer;
    CmdBufferState state;
    std::string name;
    uint32_t destroyAttempts = 0;
    uint64_t pendingSignalValue;
    VkSemaphore pendingSemaphore;
};

const char *cmdBufferStateToString(CmdBufferState state);

class CommandPool;

class CommandBuffer {
  public:
    // creates 1 command buffer
    CommandBuffer(std::shared_ptr<CommandPool> commandPool, const std::string &name = "unnamed");
    // creates 1 command buffer from a VkCommandBuffer
    CommandBuffer(std::shared_ptr<CommandPool> commandPool, VkCommandBuffer commandBuffer, const std::string &name = "unnamed");
    ~CommandBuffer();

    VkCommandBuffer getCommandBufferVk() const { return m_commandBuffer; }
    CmdBufferState getState();
    const std::string &getName() const { return m_name; }
    void setName(const std::string &name) { m_name = name; }

    bool reset(VkCommandBufferResetFlags = 0);
    VkResult begin(VkCommandBufferUsageFlags flags = 0);
    VkResult end();

    VkCommandBuffer prepareSubmit();

    void completeSubmit(VkSemaphore timelineSemaphore, uint64_t signalValue);

    void abortSubmit();

    bool canSubmit();
    bool canReset();
    bool canBegin();

    // creates many command buffers
    static std::vector<std::shared_ptr<CommandBuffer>> createCommandBuffers(std::shared_ptr<CommandPool> commandPool,
                                                                            uint32_t count, const std::string &namePrefix = "cmd");

  private:
    void updatePendingState();

    VkCommandBuffer m_commandBuffer;
    std::shared_ptr<CommandPool> m_commandPool;

    VkDevice m_device;
    CmdBufferState m_state;
    std::string m_name;
    mutable std::mutex m_stateMutex;

    VkSemaphore m_pendingSemaphore = VK_NULL_HANDLE;
    uint64_t m_pendingSignalValue = 0;
    bool m_oneTimeSubmit = false;
};
} // namespace Rapture
