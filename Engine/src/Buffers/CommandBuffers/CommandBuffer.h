#ifndef RAPTURE__COMMANDBUFFER_H
#define RAPTURE__COMMANDBUFFER_H

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

enum class CmdBufferLevel {
    PRIMARY,
    SECONDARY
};

struct SecondaryBufferInheritance {
    std::vector<VkFormat> colorFormats;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
    VkRenderingFlags renderingFlags = 0;
    uint32_t viewMask = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
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
    CommandBuffer(CommandPool *commandPool, CmdBufferLevel level, const std::string &name = "unnamed");
    CommandBuffer(CommandPool *commandPool, VkCommandBuffer commandBuffer, CmdBufferLevel level,
                  const std::string &name = "unnamed");
    ~CommandBuffer();

    VkCommandBuffer getCommandBufferVk() const { return m_commandBuffer; }
    CommandPool *getCommandPool() const { return m_commandPool; }
    CmdBufferLevel getLevel() const { return m_level; }
    bool isSecondary() const { return m_level == CmdBufferLevel::SECONDARY; }
    const std::string &getName() const { return m_name; }
    void setName(const std::string &name) { m_name = name; }

    VkResult begin(VkCommandBufferUsageFlags flags = 0);
    VkResult beginSecondary(const SecondaryBufferInheritance &inheritance,
                            VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VkResult end();

    void executeSecondary(const CommandBuffer &secondary);
    void executeSecondaries(const std::vector<const CommandBuffer*> &secondaries);

    static std::vector<std::unique_ptr<CommandBuffer>> createCommandBuffers(CommandPool *commandPool, uint32_t count,
                                                                            const std::string &namePrefix = "cmd");
    static std::vector<std::unique_ptr<CommandBuffer>> createSecondaryCommandBuffers(CommandPool *commandPool, uint32_t count,
                                                                                     const std::string &namePrefix = "secondary");

  private:
    void updatePendingState();

    VkCommandBuffer m_commandBuffer;
    CommandPool *m_commandPool;

    VkDevice m_device;
    CmdBufferLevel m_level = CmdBufferLevel::PRIMARY;
    std::string m_name;
};

} // namespace Rapture

#endif // RAPTURE__COMMANDBUFFER_H
