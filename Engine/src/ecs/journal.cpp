#include "journal.h"

#include "utils/rp_assert.h"

#include <bit>

namespace Rapture {
namespace ecs {

Journal::Journal(uint32_t channelCount, uint32_t ringCapacity)
{
    RP_ASSERT(channelCount <= CHANNEL_MAX, "channel count exceeds the width of ChangeMask");
    RP_ASSERT(ringCapacity > 0, "a journal channel needs a ring");

    m_channels.resize(channelCount);
    for (auto &channel : m_channels) {
        channel.ring.resize(ringCapacity, ENTITY_NULL);
    }
}

void Journal::record(Entity entity, ChangeMask channels)
{
    uint32_t index = EntityIndex(entity);

    while (channels != 0) {
        uint32_t channelIndex = static_cast<uint32_t>(std::countr_zero(channels));
        channels &= channels - 1;

        RP_ASSERT(channelIndex < m_channels.size(), "recording on a channel the journal does not have");
        Channel &channel = m_channels[channelIndex];

        if (index >= channel.stamps.size()) {
            continue;
        }
        if (channel.stamps[index] == m_epoch) {
            continue;
        }

        channel.stamps[index] = m_epoch;
        channel.ring[channel.total % channel.ring.size()] = entity;
        channel.total++;
    }
}

Batch Journal::readSince(uint32_t channel, Bookmark &bookmark)
{
    RP_ASSERT(channel < m_channels.size(), "reading a channel the journal does not have");

    m_epoch++;

    Channel &target = m_channels[channel];
    bool needsRebuild = !bookmark.primed || (target.total - bookmark.position) > target.ring.size();
    uint64_t begin = needsRebuild ? target.total : bookmark.position;

    bookmark.position = target.total;
    bookmark.primed = true;

    return Batch(&target.ring, begin, target.total, needsRebuild);
}

void Journal::growTo(uint32_t entityIndexCount)
{
    for (auto &channel : m_channels) {
        if (channel.stamps.size() < entityIndexCount) {
            channel.stamps.resize(entityIndexCount, 0);
        }
    }
}

uint32_t Journal::getChannelCount() const
{
    return static_cast<uint32_t>(m_channels.size());
}

} // namespace ecs
} // namespace Rapture
