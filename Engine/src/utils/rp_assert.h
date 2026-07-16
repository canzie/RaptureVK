#ifndef RAPTURE__ASSERT_H
#define RAPTURE__ASSERT_H

#include <cstdlib>
#include <format>

#include "logging/Log.h"

#ifdef NDEBUG

#define RP_ASSERT(expr, ...) ((void)0)

#else

/**
 * @brief Log a std::format message and abort when expr is false
 */
#define RP_ASSERT(expr, ...)                                                                                                   \
    do {                                                                                                                       \
        if (!(expr)) {                                                                                                         \
            RP_CORE_CRITICAL("assertion '{}' failed: {}", #expr, std::format(__VA_ARGS__));                                    \
            std::abort();                                                                                                      \
        }                                                                                                                      \
    } while (false)

#endif // NDEBUG

#endif //  RAPTURE__ASSERT_H
