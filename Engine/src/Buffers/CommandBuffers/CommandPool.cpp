#include "CommandPool.h"

#include "Logging/Log.h"
#include "WindowContext/Application.h"

#include "CommandBuffer.h"

namespace Rapture {

    std::unordered_map<uint32_t, std::shared_ptr<CommandPool>> CommandPoolManager::m_commandPools;

    CommandPool::CommandPool(const CommandPoolConfig &config)
    : m_hash(config.hash()), m_createInfo{}
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
            throw std::runtime_error("failed to create command pool!");
        }
    }

    CommandPool::~CommandPool()
    {
        m_savedCommandBuffers.clear();
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
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
        //m_commandPools.clear();
    }

    
    void CommandPoolManager::shutdown()
    {
        closeAllPools();
    }

    std::shared_ptr<CommandPool> CommandPoolManager::createCommandPool(const CommandPoolConfig &config)
    {
        if (m_commandPools.find(config.hash()) != m_commandPools.end()) {
            return m_commandPools[config.hash()];
        }

        auto commandPool = std::make_shared<CommandPool>(config);
        m_commandPools.insert(std::make_pair(config.hash(), commandPool));

        return commandPool;
    }

    /*
    std::shared_ptr<CommandPool> CommandPoolManager::getCommandPool(const CommandPoolConfig& config, bool isStrict)
    {
        uint32_t hash = config.hash();

        return getCommandPool(hash);
    }
    */

    std::shared_ptr<CommandPool> CommandPoolManager::getCommandPool(uint32_t CPHash)
    {
        if (m_commandPools.find(CPHash) == m_commandPools.end()) {
            RP_CORE_ERROR("CommandPoolManager::getCommandPool - command pool not found!");
            return nullptr;
        }

        return m_commandPools[CPHash];
    }

    /*
    void CommandPoolManager::closePool(uint32_t CPHash)
    {
        if (m_commandPools.find(CPHash) == m_commandPools.end()) {
            RP_CORE_ERROR("CommandPoolManager::closePool - command pool not found!");
            return;
        }

        m_commandPools.erase(CPHash);
    }
    */

    void CommandPoolManager::closeAllPools()
    {
        m_commandPools.clear();
    }
    
}