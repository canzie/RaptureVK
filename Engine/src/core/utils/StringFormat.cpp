#include "core/utils/StringFormat.h"

#include <cstdio>

namespace Rapture {

std::string StringFormat_bytesToUnitString(uintmax_t bytes)
{
    static constexpr const char *UNITS[] = {"B", "KB", "MB", "GB", "TB"};
    static constexpr int LAST_UNIT = 4;

    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < LAST_UNIT) {
        value /= 1024.0;
        unit++;
    }

    char buffer[32];
    if (unit == 0) {
        std::snprintf(buffer, sizeof(buffer), "%llu %s", static_cast<unsigned long long>(bytes), UNITS[unit]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, UNITS[unit]);
    }
    return buffer;
}

} // namespace Rapture
