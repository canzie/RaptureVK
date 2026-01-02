#ifndef RAPTURE__LOCKFREEBUCKETMAP_H
#define RAPTURE__LOCKFREEBUCKETMAP_H

#include "LockFreeStack.h"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace Rapture {

template <typename Key, typename Value, size_t BucketCount = 1024, typename Hash = std::hash<Key>> class LockFreeBucketMap {
  public:
    using Node = StackNode<Value>;

    LockFreeBucketMap() = default;

    LockFreeBucketMap(const LockFreeBucketMap &) = delete;
    LockFreeBucketMap &operator=(const LockFreeBucketMap &) = delete;

    void add(const Key &key, Value &&value)
    {
        Node *node = new Node(std::move(value));
        size_t idx = Hash{}(key) & (BucketCount - 1);
        m_buckets[idx].push(node);
    }

    std::vector<Value> stealAll(const Key &key)
    {
        size_t idx = Hash{}(key) & (BucketCount - 1);
        Node *list = m_buckets[idx].stealAll();

        std::vector<Value> result;
        while (list) {
            Node *next = list->next.load(std::memory_order_relaxed);
            result.push_back(std::move(list->data));
            delete list;
            list = next;
        }

        return result;
    }

    template <typename Predicate> std::vector<Value> stealMatching(const Key &key, Predicate predicate)
    {
        size_t idx = Hash{}(key) & (BucketCount - 1);
        Node *list = m_buckets[idx].stealAll();

        if (!list) {
            return {};
        }

        std::vector<Value> result;
        Node *nonMatching = nullptr;

        while (list) {
            Node *next = list->next.load(std::memory_order_relaxed);

            if (predicate(key, list->data)) {
                result.push_back(std::move(list->data));
                delete list;
            } else {
                list->next.store(nonMatching, std::memory_order_relaxed);
                nonMatching = list;
            }

            list = next;
        }

        // Re-add non-matching nodes
        while (nonMatching) {
            Node *next = nonMatching->next.load(std::memory_order_relaxed);
            m_buckets[idx].push(nonMatching);
            nonMatching = next;
        }

        return result;
    }

  private:
    std::array<LockFreeStack<Value>, BucketCount> m_buckets;
};

} // namespace Rapture

#endif // RAPTURE__LOCKFREEBUCKETMAP_H
