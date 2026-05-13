#pragma once

#include <cstdint>

namespace Rapture {

using EntityID = uint32_t;

enum Mobility {
    MOBILITY_STATIC = 0,
    MOBILITY_DYNAMIC,
    MOBILITY_COUNT
};

}