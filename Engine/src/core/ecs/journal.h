#ifndef RAPTURE__JOURNAL_H
#define RAPTURE__JOURNAL_H

#include "common.h"

#include <vector>

namespace Rapture {
namespace ecs {

using ChangeMask = uint32_t;

inline constexpr uint32_t CHANNEL_MAX = 32;
inline constexpr uint32_t JOURNAL_RING_CAPACITY = 4096;

/**
 * @brief Mask bit of a channel index, so callers never write shifts.
 * @param channel Channel index, below the journal's channel count.
 * @return Mask with only that channel's bit set.
 */
inline constexpr ChangeMask ChannelBit(uint32_t channel)
{
    return ChangeMask(1) << channel;
}

/**
 * @brief One destination's position in one channel.
 *
 * Belongs to whatever holds the data being kept in sync, so N buffers in flight means N of these.
 */
struct Bookmark {
    uint64_t position = 0;
    bool primed = false;
};

/**
 * @brief The entities recorded on one channel since a bookmark was last read.
 */
class Batch {
  public:
    Batch(const std::vector<Entity> *ring, uint64_t begin, uint64_t end, bool needsRebuild)
        : m_ring(ring), m_begin(begin), m_end(end), m_needsRebuild(needsRebuild)
    {
    }

    /**
     * @brief Whether the reader fell far enough behind that records were overwritten.
     * @return True if the consumer must rebuild from scratch instead of walking this batch.
     */
    bool needsRebuild() const { return m_needsRebuild; }

    uint32_t getCount() const { return static_cast<uint32_t>(m_end - m_begin); }

    class Iterator {
      public:
        Iterator(const std::vector<Entity> *ring, uint64_t position) : m_ring(ring), m_position(position) {}

        Iterator &operator++()
        {
            m_position++;
            return *this;
        }

        bool operator!=(const Iterator &other) const { return m_position != other.m_position; }

        Entity operator*() const { return (*m_ring)[m_position % m_ring->size()]; }

      private:
        const std::vector<Entity> *m_ring;
        uint64_t m_position;
    };

    Iterator begin() const { return Iterator(m_ring, m_begin); }

    Iterator end() const { return Iterator(m_ring, m_end); }

  private:
    const std::vector<Entity> *m_ring;
    uint64_t m_begin;
    uint64_t m_end;
    bool m_needsRebuild;
};

/**
 * @brief Append only record of which entities changed, per channel, read by position.
 *
 * The channels themselves are the engine's to name. The journal only knows how many there are.
 */
class Journal {
  public:
    /**
     * @brief Builds a journal with a fixed number of channels.
     * @param channelCount Number of channels, at most CHANNEL_MAX.
     * @param ringCapacity Records retained per channel before a reader has to rebuild.
     */
    explicit Journal(uint32_t channelCount, uint32_t ringCapacity = JOURNAL_RING_CAPACITY);

    /**
     * @brief Records that an entity changed on every channel in a mask.
     * @param entity Entity that changed.
     * @param channels Mask of channels to record on, may be zero.
     */
    void record(Entity entity, ChangeMask channels);

    /**
     * @brief Reads everything recorded on a channel since a bookmark, and advances it.
     * @param channel Channel index to read.
     * @param bookmark Reader's position, advanced to the end of the channel.
     * @return The batch, which may instead ask the caller to rebuild.
     */
    Batch readSince(uint32_t channel, Bookmark &bookmark);

    /**
     * @brief Grows the per entity arrays to cover an entity index.
     * @param entityIndexCount Number of entity slots that must be addressable.
     */
    void growTo(uint32_t entityIndexCount);

    uint32_t getChannelCount() const;

  private:
    struct Channel {
        std::vector<uint32_t> stamps;
        std::vector<Entity> ring;
        uint64_t total = 0;
    };

    std::vector<Channel> m_channels;
    uint32_t m_epoch = 1;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__JOURNAL_H
