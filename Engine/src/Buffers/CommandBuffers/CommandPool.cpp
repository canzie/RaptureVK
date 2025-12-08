#include "CommandPool.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"
#include "CommandBuffer.h"

namespace Rapture {

    std::unordered_map<CommandPoolHash, std::shared_ptr<CommandPool>> CommandPoolManager::s_commandPools;

    CommandPool::CommandPool(const CommandPoolConfig &config)
    : m_hash(config.hash()), m_createInfo{}, m_commandPool(VK_NULL_HANDLE), m_device(VK_NULL_HANDLE)
    {

        auto& app = Application::getInstance();
        auto device = app.getVulkanContext().getLogicalDevice();

        m_device = device;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = config.flags;
        poolInfo.queueFamilyIndex = config.queueFamilyIndex;

        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            RP_CORE_ERROR("failed to create command pool!");
            m_commandPool = VK_NULL_HANDLE;
        }
    }

    CommandPool::~CommandPool()
    {
        m_savedCommandBuffers.clear();
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    } 


    std::shared_ptr<CommandBuffer> CommandPool::getCommandBuffer(bool stayAlive)
    {
        auto commandBuffer = std::make_shared<CommandBuffer>(shared_from_this());
        if (stayAlive) {
            m_savedCommandBuffers.push_back(commandBuffer);
        }
        return commandBuffer;
        
    }

    std::vector<std::shared_ptr<CommandBuffer>> CommandPool::getCommandBuffers(uint32_t count)
    {
        return CommandBuffer::createCommandBuffers(shared_from_this(), count);
    }

    void CommandPoolManager::init()
    {
        //s_commandPools.clear();
    }

    
    void CommandPoolManager::shutdown()
    {
        closeAllPools();
    }

    std::shared_ptr<CommandPool> CommandPoolManager::createCommandPool(const CommandPoolConfig &config)
    {
        if (s_commandPools.find(config.hash()) != s_commandPools.end()) {
            return s_commandPools[config.hash()];
        }

        auto commandPool = std::make_shared<CommandPool>(config);
        if (commandPool->getCommandPoolVk() == VK_NULL_HANDLE) {
            RP_CORE_ERROR("Failed to create command pool!");
            return nullptr;
        }

        s_commandPools.insert(std::make_pair(config.hash(), commandPool));

        return commandPool;
    }

    /*
    std::shared_ptr<CommandPool> CommandPoolManager::getCommandPool(const CommandPoolConfig& config, bool isStrict)
    {
        uint32_t hash = config.hash();

        return getCommandPool(hash);
    }
    */

    std::shared_ptr<CommandPool> CommandPoolManager::getCommandPool(CommandPoolHash cpHash)
    {
        if (s_commandPools.find(cpHash) == s_commandPools.end()) {
            RP_CORE_ERROR("Command pool not found!");
            return nullptr;
        }

        return s_commandPools[cpHash];
    }

    /*
    void CommandPoolManager::closePool(uint32_t CPHash)
    {
        if (s_commandPools.find(CPHash) == s_commandPools.end()) {
            RP_CORE_ERROR("CommandPoolManager::closePool - command pool not found!");
            return;
        }

        s_commandPools.erase(CPHash);
    }
    */

    void CommandPoolManager::closeAllPools()
    {
        s_commandPools.clear();
    }
    
}