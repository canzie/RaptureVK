#ifndef RAPTURE__ENTITYCOMMON_H
#define RAPTURE__ENTITYCOMMON_H

#include "ecs/common.h"

namespace Rapture {

enum Mobility {
    MOBILITY_STATIC = 0,
    MOBILITY_DYNAMIC,
    MOBILITY_COUNT
};

inline const char *mobilityToString(Mobility mobility)
{
    static constexpr const char *names[MOBILITY_COUNT] = {"Static", "Dynamic"};
    return mobility < MOBILITY_COUNT ? names[mobility] : "Unknown";
}

} // namespace Rapture

#endif // RAPTURE__ENTITYCOMMON_H
