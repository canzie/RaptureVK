#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

// Forward declaration
class CommandPool;

class CommandBuffer {
  public:
    // creates 1 command buffer
    CommandBuffer(std::shared_ptr<CommandPool> commandPool);
    // creates 1 command buffer from a VkCommandBuffer
    CommandBuffer(std::shared_ptr<CommandPool> commandPool, VkCommandBuffer commandBuffer);
    ~CommandBuffer();

    VkCommandBuffer getCommandBufferVk() const { return m_commandBuffer; }

    void reset(VkCommandBufferResetFlags = 0);
    VkResult end();
    // creates many command buffers
    static std::vector<std::shared_ptr<CommandBuffer>> createCommandBuffers(std::shared_ptr<CommandPool> commandPool,
                                                                            uint32_t count);

  private:
    VkCommandBuffer m_commandBuffer;
    std::shared_ptr<CommandPool> m_commandPool;

    VkDevice m_device;
};
} // namespace Rapture
