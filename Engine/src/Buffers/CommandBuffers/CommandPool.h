#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <string>

namespace Rapture {

    // Forward declaration
    class CommandBuffer;

    struct CommandPoolConfig {
        std::string name = "CommandPool";
        size_t threadId=0;
        uint32_t queueFamilyIndex;
        VkCommandPoolCreateFlags flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        uint32_t hash() const {
            return std::hash<size_t>()(threadId) ^ std::hash<uint32_t>()(queueFamilyIndex) ^ std::hash<VkCommandPoolCreateFlags>()(flags);
        }
    };


    // should automatically create a command pool for each queue family in a thread, 
    // not for each call but use a manager system to create a new pool when the conditions are met
    // could also allow for manual specs.
    class CommandPool : public std::enable_shared_from_this<CommandPool> {
        public:
            CommandPool(const CommandPoolConfig& config);
            ~CommandPool();

            VkCommandPool getCommandPoolVk() const { return m_commandPool; }
            std::shared_ptr<CommandBuffer> getCommandBuffer(bool stayAlive=false);
            std::vector<std::shared_ptr<CommandBuffer>> getCommandBuffers(uint32_t count);

        private:
            //std::vector<std::shared_ptr<CommandBuffer>> m_commandBuffers;
            VkCommandPoolCreateInfo m_createInfo;
            VkCommandPool m_commandPool;
            uint32_t m_hash;

            VkDevice m_device;

            // only added when stayAlive=true, can be usefull for liefetime commandbuffers which rapture does not manage directly
            std::vector<std::shared_ptr<CommandBuffer>> m_savedCommandBuffers;

    };


    class CommandPoolManager {
    public:
        static void init();
        static void shutdown();

        static std::shared_ptr<CommandPool> createCommandPool(const CommandPoolConfig& config);
        // access a pool by its hash
        static std::shared_ptr<CommandPool> getCommandPool(uint32_t CPHash);
        // the strict flag will return the command pool closest to the config provided (most same values)
        // assuming atleast 1 pool exists, this function will return a pool, given the strict flag=false
        //static std::shared_ptr<CommandPool> getCommandPool(const CommandPoolConfig& config, bool isStrict = true);


        // currently still needs to destroy manually because when destroying some other thng ith a ptr
        // to the bool could be using it, causing a crash or undefined behavior
        // would probably have to either switch to weak ptrs or checking the poolvk value every time when retireving it
        //static void closePool(uint32_t CPHash);
        static void closeAllPools();

    private:
        static std::unordered_map<uint32_t, std::shared_ptr<CommandPool>> m_commandPools;


    };

}
