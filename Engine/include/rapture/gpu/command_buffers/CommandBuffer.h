#ifndef RAPTURE__COMMANDBUFFER_H
#define RAPTURE__COMMANDBUFFER_H

#include <rapture/gpu/command_buffers/CommandBufferLease.h>

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace Rapture {

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

class CommandPool;

class CommandBuffer {
  public:
    CommandBuffer(CommandPool *commandPool, CmdBufferLevel level, const std::string &name = "unnamed");
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

    /**
     * @brief Records a secondary command buffer into this one, holding its lease until this buffer is submitted
     * @param secondary The recorded secondary
     */
    void executeSecondary(CommandBufferLease secondary);

    /**
     * @brief The secondaries recorded into this buffer since it was last submitted
     */
    const std::vector<CommandBufferLease> &getSecondaries() const { return m_secondaries; }

    /**
     * @brief Hands back the leases of every secondary recorded into this buffer
     */
    void clearSecondaries() { m_secondaries.clear(); }

  private:
    VkCommandBuffer m_commandBuffer;
    CommandPool *m_commandPool;

    VkDevice m_device;
    CmdBufferLevel m_level = CmdBufferLevel::PRIMARY;
    std::string m_name;

    std::vector<CommandBufferLease> m_secondaries;
};

} // namespace Rapture

#endif // RAPTURE__COMMANDBUFFER_H
