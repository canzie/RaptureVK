#ifndef RAPTURE__ASCRIPT_H
#define RAPTURE__ASCRIPT_H

#include "assets/asset_manager/Asset.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rapture {

/**
 * @brief A script asset: the Lua an object runs, held once however many objects run it.
 *
 * The Lua is kept either in the asset file itself or in a Lua file of its own beside it. A script
 * with a file of its own stays something an editor, a diff and a merge can all read, which a script
 * shared across a project wants; one kept in the asset file is a script nothing else was going to
 * open anyway.
 */
class AScript : public Asset {
  public:
    /**
     * @brief Builds a script
     * @param source The Lua to run
     * @param sourceIsInline Whether the Lua is kept in the asset file rather than in one of its own
     */
    AScript(std::string source, bool sourceIsInline);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::string_view source() const { return m_source; }

    /**
     * @brief Replaces the Lua this holds, without writing it anywhere
     * @param source The Lua to run
     */
    void setSource(std::string source);

    /**
     * @brief Whether the Lua is kept in the asset file rather than in a Lua file of its own
     */
    bool isSourceInline() const { return m_sourceIsInline; }

    /**
     * @brief Moves the Lua into the asset file, or out into the file the asset's provenance names
     * @param sourceIsInline Whether to keep the Lua in the asset file
     */
    void setSourceIsInline(bool sourceIsInline) { m_sourceIsInline = sourceIsInline; }

    /**
     * @brief Writes the Lua, or nothing where it is kept in a file of its own
     * @return The serialized bytes
     */
    std::vector<uint8_t> serialize() const override;

    bool reimport(const AssetMetadata &metadata) override;

    /**
     * @brief Builds a script, reading its Lua out of the blob or out of the file the metadata names
     * @param metadata The asset metadata
     * @param blob The serialized bytes
     * @return The script, or nullptr if its Lua file could not be read
     */
    static std::unique_ptr<AScript> deserialize(const AssetMetadata &metadata, std::span<const uint8_t> blob);

  private:
    std::string m_source;
    bool m_sourceIsInline = true;
};

} // namespace Rapture

#endif // RAPTURE__ASCRIPT_H
