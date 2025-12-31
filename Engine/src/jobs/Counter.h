#ifndef RAPTURE__COUNTER_H
#define RAPTURE__COUNTER_H

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace Rapture {

class JobSystem;

struct Counter {
    std::atomic<int32_t> value{0};

    void increment(int32_t amount = 1);
    void decrement(int32_t amount = 1);

    /**
     * @brief Get the current value of the counter
     * @return The current value of the counter
     */
    int32_t get() const;

    /**
     * @brief Notify the wait list that the counter has changed
     * @param system The job system to notify
     */
    void notify(JobSystem *system);
};

class FrameCounterPool {
  public:
    static constexpr size_t COUNTERS_PER_FRAME = 256;

    void init(uint32_t framesInFlight);

    Counter *acquire(); // Get a counter for this frame
    void beginFrame();  // Reset current frame's counters
    void endFrame();    // Advance to next frame

  private:
    struct FrameCounters {
        std::array<Counter, COUNTERS_PER_FRAME> counters;
        std::atomic<uint32_t> nextIndex{0};
    };

    std::vector<FrameCounters> m_frames;
    uint32_t m_currentFrame = 0;
};

} // namespace Rapture

#endif // RAPTURE__COUNTER_H
