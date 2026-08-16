#include "Counter.h"
#include <atomic>
#include <cstdint>

#include "JobSystem.h"

namespace Rapture {

void Counter::increment(int32_t amount)
{
    value.fetch_add(amount, std::memory_order_release);
    notify(&jobs());
}

void Counter::decrement(int32_t amount)
{
    value.fetch_sub(amount, std::memory_order_release);
    notify(&jobs());
}

void Counter::notify(JobSystem *system)
{
    system->getWaitList().onCounterChanged(this);
}

int32_t Counter::get() const
{
    return value.load(std::memory_order_acquire);
}

} // namespace Rapture
