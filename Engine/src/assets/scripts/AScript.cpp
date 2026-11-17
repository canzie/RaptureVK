#include <rapture/assets/scripts/AScript.h>

#include <rapture/core/utils/Log.h>

#include <filesystem>
#include <fstream>

namespace Rapture {

AScript::AScript(std::string source) : m_source(std::move(source)) {}

const TypeInfo &AScript::staticType()
{
    static const TypeInfo type("AScript", &Asset::staticType());
    return type;
}

const TypeInfo &AScript::type() const
{
    return staticType();
}

std::vector<uint8_t> AScript::serialize() const
{
    return {};
}

bool AScript::reimport(const AssetMetadata &metadata)
{
    std::filesystem::path path = metadata.getSourcePath();
    if (path.empty()) {
        RP_CORE_ERROR("'{}' names no Lua file", metadata.getName());
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        RP_CORE_ERROR("Could not read '{}'", path.string());
        return false;
    }

    m_source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

} // namespace Rapture
