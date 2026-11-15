#ifndef RAPTURE__PRIORITY_QUEUE_H
#define RAPTURE__PRIORITY_QUEUE_H

#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>

namespace Rapture {

/**
 * @brief A stable priority queue: higher priorities are served first, entries sharing a priority keep insertion order
 */
template <typename T>
class PriorityQueue {
  public:
    bool empty() const { return m_queue.empty(); }
    size_t size() const { return m_queue.size(); }

    /**
     * @brief Inserts an entry behind any entry already at its priority
     * @param value The entry to insert
     * @param priority The priority, higher is served first
     */
    void push(T value, int32_t priority) { m_queue.push(Entry{priority, m_nextSeq++, std::move(value)}); }

    const T &front() const { return m_queue.top().value; }

    /**
     * @brief Removes the front entry and hands back ownership of it
     * @return The front entry, moved out
     */
    T pop()
    {
        T value = std::move(const_cast<Entry &>(m_queue.top()).value);
        m_queue.pop();
        return value;
    }

    void clear() { m_queue = {}; }

  private:
    struct Entry {
        int32_t priority;
        uint64_t seq;
        T value;

        bool operator<(const Entry &other) const
        {
            return priority != other.priority ? priority < other.priority : seq > other.seq;
        }
    };

    std::priority_queue<Entry> m_queue;
    uint64_t m_nextSeq = 0;
};

} // namespace Rapture

#endif // RAPTURE__PRIORITY_QUEUE_H
