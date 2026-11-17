#ifndef RAPTURE__ASCRIPT_H
#define RAPTURE__ASCRIPT_H

#include <rapture/assets/asset_manager/Asset.h>

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
     * @brief Writes nothing, the Lua file is the content
     * @return An empty payload
     */
    std::vector<uint8_t> serialize() const override;

    bool reimport(const AssetMetadata &metadata) override;

  private:
    std::string m_source;
};

} // namespace Rapture

#endif // RAPTURE__ASCRIPT_H
