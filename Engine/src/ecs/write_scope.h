#ifndef RAPTURE__WRITE_SCOPE_H
#define RAPTURE__WRITE_SCOPE_H

#include "journal.h"

namespace Rapture {
namespace ecs {

/**
 * @brief Channels a component type invalidates when written, specialize per component type.
 *
 * The default records nothing, so a component only enters the journal once it says it should.
 */
template <typename T>
struct ComponentTraits {
    static constexpr ChangeMask CHANGE_CHANNELS = 0;
};

/**
 * @brief Mutable access to one component, recorded in the journal when it goes out of scope.
 */
template <typename T>
class WriteScope {
  public:
    WriteScope(T *data, Journal *journal, Entity entity, ChangeMask channels)
        : m_data(data), m_journal(journal), m_entity(entity), m_channels(channels)
    {
    }

    ~WriteScope()
    {
        if (m_journal != nullptr && m_channels != 0) {
            m_journal->record(m_entity, m_channels);
        }
    }

    WriteScope(const WriteScope &) = delete;
    WriteScope &operator=(const WriteScope &) = delete;

    T *operator->() const { return m_data; }

    T &operator*() const { return *m_data; }

  private:
    T *m_data;
    Journal *m_journal;
    Entity m_entity;
    ChangeMask m_channels;
};

} // namespace ecs
} // namespace Rapture

#endif // RAPTURE__WRITE_SCOPE_H
