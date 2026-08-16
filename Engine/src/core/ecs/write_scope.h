#ifndef RAPTURE__WRITE_SCOPE_H
#define RAPTURE__WRITE_SCOPE_H

#include "journal.h"

#include <concepts>

namespace Rapture {
namespace ecs {

/**
 * @brief Satisfied by a component that declares what its writes invalidate.
 */
template <typename T>
concept DeclaresChannels = requires {
    { T::CHANGE_CHANNELS } -> std::convertible_to<ChangeMask>;
};

/**
 * @brief Channels a component type invalidates when written, zero unless it declares any.
 */
template <typename T>
inline constexpr ChangeMask COMPONENT_CHANNELS = 0;

template <DeclaresChannels T>
inline constexpr ChangeMask COMPONENT_CHANNELS<T> = T::CHANGE_CHANNELS;

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
