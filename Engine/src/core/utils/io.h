#ifndef RAPTURE__IO_H
#define RAPTURE__IO_H

#include <filesystem>
#include <string>
#include <vector>

namespace Rapture {

/**
 * Reads a file and returns its contents as a vector of characters.
 * @param path The path to the file to read
 * @return A vector containing the file's binary contents, or an empty vector if the file cannot be opened
 */
std::vector<char> readFile(const std::filesystem::path &path);

/**
 * Reads a file and returns its contents as a string.
 * @param path The path to the file to read
 * @return A string containing the file's contents, or an empty string if the file cannot be opened
 */
std::string readFileAsString(const std::filesystem::path &path);

} // namespace Rapture

#endif // RAPTURE__IO_H
