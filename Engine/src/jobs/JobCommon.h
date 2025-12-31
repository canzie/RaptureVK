#ifndef RAPTURE__JOBCOMMON_H
#define RAPTURE__JOBCOMMON_H

namespace Rapture {

enum class JobPriority {
    LOW,    // Background work, can be starved
    NORMAL, // Default priority
    HIGH    // Latency-sensitive (frame-critical rendering)
};

enum QueueAffinity {
    ANY,
    GRAPHICS,
    COMPUTE,
    TRANSFER,
    AFFINITY_COUNT
};

} // namespace Rapture

#endif // RAPTURE__JOBCOMMON_H
