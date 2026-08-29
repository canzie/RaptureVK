#include "AScript.h"

#include "core/utils/Log.h"

#include <filesystem>
#include <fstream>

namespace Rapture {

AScript::AScript(std::string source, bool sourceIsInline) : m_source(std::move(source)), m_sourceIsInline(sourceIsInline) {}

const TypeInfo &AScript::staticType()
{
    static const TypeInfo type("AScript", &Asset::staticType());
    return type;
}

const TypeInfo &AScript::type() const
{
    return staticType();
}

void AScript::setSource(std::string source)
{
    m_source = std::move(source);
}

std::vector<uint8_t> AScript::serialize() const
{
    if (!m_sourceIsInline) {
        return {};
    }

    return std::vector<uint8_t>(m_source.begin(), m_source.end());
}

bool AScript::reimport(const AssetMetadata &metadata)
{
    std::filesystem::path path = metadata.getSourcePath();
    if (path.empty()) {
        RP_CORE_ERROR("'{}' keeps its Lua in its own asset file, so there is nothing to read it from",
                      metadata.getName());
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        RP_CORE_ERROR("Could not read '{}'", path.string());
        return false;
    }

    m_source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    m_sourceIsInline = false;
    return true;
}

std::unique_ptr<AScript> AScript::deserialize(const AssetMetadata &metadata, std::span<const uint8_t> blob)
{
    if (!metadata.getSourcePath().empty()) {
        auto script = std::make_unique<AScript>(std::string{}, false);
        return script->reimport(metadata) ? std::move(script) : nullptr;
    }

    std::string source;
    source.assign(blob.begin(), blob.end());
    return std::make_unique<AScript>(std::move(source), true);
}

} // namespace Rapture
