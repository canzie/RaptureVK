#ifndef RAPTURE__ASSET_REGISTRY_H
#define RAPTURE__ASSET_REGISTRY_H

#include "assets/asset_manager/Asset.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace Rapture {

/**
 * @brief What one class of asset is: what it is called, what it is written as, and how it is read back.
 *
 * A code is the class's identity in a file rather than its place in the AssetType enum, so the enum
 * is free to gain, lose and reorder entries. A code is never reused for a different class. An
 * extension only labels a file for the user and their tools, so related classes share one.
 */
struct AssetClass {
    AssetType assetType = ASSET_NONE;
    uint32_t code = 0;
    std::string_view extension;
    std::string_view displayName;

    /**
     * @brief Builds an asset of this class from the payload its asset file holds
     */
    std::unique_ptr<Asset> (*deserialize)(std::span<const uint8_t> payload) = nullptr;

    /**
     * @brief Builds an asset of this class from the external file its metadata names
     */
    std::unique_ptr<Asset> (*import)(AssetMetadata &metadata) = nullptr;
};

/**
 * @brief Every class of asset the engine can hold
 */
class AssetRegistry {
  public:
    static const AssetClass *find(AssetType type);

    /**
     * @brief The class whose files carry a code
     * @param code The four character code read out of a file header
     * @return The class, or nullptr if no class writes that code
     */
    static const AssetClass *findByCode(uint32_t code);

    /**
     * @brief The first class written with an extension, for picking a class from a path
     * @param extension The extension, leading dot included
     * @return The class, or nullptr if no class is written with that extension
     */
    static const AssetClass *findByExtension(std::string_view extension);

    /**
     * @brief Whether an extension is one Rapture writes its own assets with
     * @param extension The extension to test, leading dot included
     * @return True if a class is written with that extension
     */
    static bool isRaptureExtension(std::string_view extension);

    static std::span<const AssetClass> classes();
};

} // namespace Rapture

#endif // RAPTURE__ASSET_REGISTRY_H
