#pragma once

#include <cstdint>

namespace Rapture {

using EntityID = uint32_t;

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
