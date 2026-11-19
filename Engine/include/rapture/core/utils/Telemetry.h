#ifndef RAPTURE__TELEMETRY_H
#define RAPTURE__TELEMETRY_H

#include <cstdint>
#include <vector>

namespace Rapture {

/**
 * @brief A snapshot of hardware and frame timing readings
 */
struct Telemetry {
    uint64_t vramUsedBytes = 0;   ///< device-local heap usage, other processes included
    uint64_t vramBudgetBytes = 0; ///< driver-reported device-local budget

    uint64_t ramUsedBytes = 0;
    uint64_t ramTotalBytes = 0;

    std::vector<float> cpuUsagePerCore; ///< percent per logical core
    float gpuTemperatureC = 0.0f;
    float cpuTemperatureC = 0.0f;

    float frameTimeMs = 0.0f; ///< mean over the last sampling interval
};

} // namespace Rapture

#endif // RAPTURE__TELEMETRY_H
