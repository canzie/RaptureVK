#ifndef RAPTURE__TICK_PHASE_H
#define RAPTURE__TICK_PHASE_H

namespace Rapture {

/**
 * @brief When in a frame an instance is updated.
 *
 * Registration order within a phase is arbitrary, so anything that has to read what another
 * instance wrote says so by sitting in a later phase rather than by being registered later.
 */
enum TickPhase {
    TICK_INPUT,
    TICK_PRE_PHYSICS,
    TICK_POST_PHYSICS,
    TICK_COUNT
};

} // namespace Rapture

#endif // RAPTURE__TICK_PHASE_H
