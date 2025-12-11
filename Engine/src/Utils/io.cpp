#include "io.h"
#include "Logging/Log.h"

#include <fstream>

namespace Rapture {

std::vector<char> readFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        RP_CORE_ERROR("failed to open file! {0}", path.string());
        return {};
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}

} // namespace Rapture
