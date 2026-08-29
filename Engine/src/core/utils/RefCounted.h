#ifndef RAPTURE__REF_COUNTED_H
#define RAPTURE__REF_COUNTED_H

#include "core/utils/Typed.h"

#include <atomic>
#include <cstdint>

namespace Rapture {

/**
 * @brief Base of anything counted by the users holding it rather than owned by them.
 *
 * One owner holds the object for its whole life, so the count reaching zero is something that owner
 * acts on rather than something that destroys the object.
 */
class RefCounted : public Typed {
  public:
    RefCounted(const RefCounted &) = delete;
    RefCounted &operator=(const RefCounted &) = delete;

    void addRef() { m_useCount.fetch_add(1, std::memory_order_relaxed); }

    /**
     * @brief Gives up one use
     * @return True if this was the last use
     */
    bool releaseRef() { return m_useCount.fetch_sub(1, std::memory_order_acq_rel) == 1; }

    uint32_t useCount() const { return m_useCount.load(std::memory_order_relaxed); }

    /**
     * @brief Called once the last user has let this object go
     */
    virtual void onLastUseReleased() {}

  protected:
    RefCounted() = default;

  private:
    std::atomic<uint32_t> m_useCount{0};
};

} // namespace Rapture

#endif // RAPTURE__REF_COUNTED_H
