#ifndef RAPTURE__LOCKFREESTACK_H
#define RAPTURE__LOCKFREESTACK_H

#include <atomic>

namespace Rapture {

template <typename T> struct StackNode {
    std::atomic<StackNode *> next{nullptr};
    T data;

    StackNode() = default;
    explicit StackNode(T &&value) : data(std::move(value)) {}
    explicit StackNode(const T &value) : data(value) {}
};

/**
 * @brief Lock-free intrusive stack (LIFO)
 *
 * Multiple producers can push concurrently.
 * Single consumer can steal the entire list atomically.
 * Nodes are single-use to avoid ABA issues.
 */
template <typename T> class LockFreeStack {
  public:
    LockFreeStack() = default;

    LockFreeStack(const LockFreeStack &) = delete;
    LockFreeStack &operator=(const LockFreeStack &) = delete;

    /**
     * @brief Push a node onto the stack (lock-free)
     * @param node The node to push (caller owns allocation)
     */
    void push(StackNode<T> *node)
    {
        StackNode<T> *oldHead;
        do {
            oldHead = m_head.load(std::memory_order_acquire);
            node->next.store(oldHead, std::memory_order_relaxed);
        } while (!m_head.compare_exchange_weak(oldHead, node, std::memory_order_release, std::memory_order_relaxed));
    }

    /**
     * @brief Atomically steal the entire stack
     * @return Head of the stolen list (caller takes ownership), nullptr if empty
     */
    StackNode<T> *stealAll() { return m_head.exchange(nullptr, std::memory_order_acq_rel); }

    /**
     * @brief Check if stack is empty
     */
    bool empty() const { return m_head.load(std::memory_order_acquire) == nullptr; }

  private:
    std::atomic<StackNode<T> *> m_head{nullptr};
};

} // namespace Rapture

#endif // RAPTURE__LOCKFREESTACK_H
