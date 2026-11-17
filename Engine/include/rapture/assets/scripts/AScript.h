#ifndef RAPTURE__ASCRIPT_H
#define RAPTURE__ASCRIPT_H

#include <rapture/assets/asset_manager/Asset.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Rapture {

/**
 * @brief A script asset: the Lua an object runs, held once however many objects run it.
 *
 * The Lua is a plain Lua file beside the asset file, which only says which asset it is.
 */
class AScript : public Asset {
  public:
    /**
     * @brief Builds a script
     * @param source The Lua to run
     */
    explicit AScript(std::string source);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::string_view source() const { return m_source; }

    /**
     * @brief Replaces the Lua this holds, without writing it anywhere
     * @param source The Lua to run
     */
    void setSource(std::string source);

    /**
     * @brief Writes nothing, the Lua lives in its own file
     * @return An empty payload
     */
    std::vector<uint8_t> serialize() const override;

    bool reimport(const AssetMetadata &metadata) override;

    /**
     * @brief Builds a script, reading its Lua out of the file the metadata names
     * @param metadata The asset metadata
     * @return The script, or nullptr if its Lua file could not be read
     */
    static std::unique_ptr<AScript> deserialize(const AssetMetadata &metadata);

  private:
    std::string m_source;
};

} // namespace Rapture

#endif // RAPTURE__ASCRIPT_H
