#ifndef RAPTURE__ASSERT_H
#define RAPTURE__ASSERT_H

#include <cstdlib>
#include <format>
#include <version>

#include "core/utils/Log.h"

#if defined(__cpp_lib_unreachable) && __cpp_lib_unreachable >= 202202L

#include <utility>

/**
 * @brief Marks a point control flow cannot reach, so the compiler may drop the path leading to it
 */
#define RP_UNREACHABLE() std::unreachable()

#elif defined(__GNUC__) || defined(__clang__)

#define RP_UNREACHABLE() __builtin_unreachable()

#elif defined(_MSC_VER)

#define RP_UNREACHABLE() __assume(0)

#else

#define RP_UNREACHABLE() ((void)0)

#endif // __cpp_lib_unreachable

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
