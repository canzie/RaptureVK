#ifndef RAPTURE__COMMAND_BUFFER_LEASE_H
#define RAPTURE__COMMAND_BUFFER_LEASE_H

#include <memory>

namespace Rapture {

class CommandBuffer;
class CommandPool;
class CommandPoolManager;

/**
 * @brief Sole hold on a command buffer and the pool it comes from, for one recording
 *
 * Nothing else records into the pool while the lease is held, so the recording may move between threads. Dropping the lease
 * hands the pool back; it is recorded into again only once the GPU has finished every submission of it.
 */
class CommandBufferLease {
  public:
    CommandBufferLease();
    CommandBufferLease(CommandPoolManager *manager, std::unique_ptr<CommandPool> pool);
    ~CommandBufferLease();

    CommandBufferLease(CommandBufferLease &&other) noexcept;
    CommandBufferLease &operator=(CommandBufferLease &&other) noexcept;

    CommandBufferLease(const CommandBufferLease &) = delete;
    CommandBufferLease &operator=(const CommandBufferLease &) = delete;

    /**
     * @brief The leased command buffer
     * @return The command buffer, nullptr for an empty lease
     */
    CommandBuffer *get() const;

    CommandBuffer *operator->() const { return get(); }

    /**
     * @brief Whether the lease holds a command buffer
     */
    bool isValid() const { return m_pool != nullptr; }

    /**
     * @brief Hands the pool back, leaving the lease empty
     */
    void release();

  private:
    CommandPoolManager *m_manager = nullptr;
    std::unique_ptr<CommandPool> m_pool;
};

} // namespace Rapture

#endif // RAPTURE__COMMAND_BUFFER_LEASE_H
