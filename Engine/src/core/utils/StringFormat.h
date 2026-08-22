#ifndef RAPTURE__STRING_FORMAT_H
#define RAPTURE__STRING_FORMAT_H

#include <cstdint>
#include <string>

namespace Rapture {

/**
 * @brief Converts a byte count to text carrying the largest unit that leaves it above one
 * @param bytes The count to convert
 * @return The count and its unit, so 1468006 becomes "1.4 MB"
 */
std::string StringFormat_bytesToUnitString(uintmax_t bytes);

} // namespace Rapture

#endif // RAPTURE__STRING_FORMAT_H
