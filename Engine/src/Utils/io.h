#ifndef RAPTURE__IO_H
#define RAPTURE__IO_H

#include <vector>
#include <filesystem>

namespace Rapture {

    /**
     * Reads a file and returns its contents as a vector of characters.
     * @param path The path to the file to read
     * @return A vector containing the file's binary contents, or an empty vector if the file cannot be opened
     */
    std::vector<char> readFile(const std::filesystem::path& path);

}

#endif // RAPTURE__IO_H
