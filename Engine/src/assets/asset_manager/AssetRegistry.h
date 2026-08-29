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
 * A magic is the class's identity in a file rather than its place in the AssetType enum, so the enum
 * is free to gain, lose and reorder entries. A magic is never reused for a different class. An
 * extension only labels a file for the user and their tools, so related classes share one.
 */
struct AssetClass {
    AssetType assetType = ASSET_NONE;
    uint32_t fileMagic = 0;
    std::string_view fileExtension;
    std::string_view displayName;

    /**
     * @brief Builds an asset of this class from the payload its asset file holds
     */
    std::unique_ptr<Asset> (*deserialize)(std::span<const uint8_t> payload) = nullptr;

    /**
     * @brief Builds an asset of this class from the external file its metadata names
     */
    std::unique_ptr<Asset> (*import)(AssetMetadata &metadata, AssetHandle handle) = nullptr;
};

/**
 * @brief Every class of asset the engine can hold
 */
class AssetRegistry {
  public:
    static const AssetClass *find(AssetType type);

    /**
     * @brief The magic the class of a type writes at the head of its files
     * @param type The type to look up
     * @return The magic
     */
    static uint32_t fileMagic(AssetType type);

    /**
     * @brief The extension the class of a type is written with
     * @param type The type to look up
     * @return The extension, leading dot included
     */
    static std::string_view fileExtension(AssetType type);

    /**
     * @brief What the class of a type is called where it is shown to the user
     * @param type The type to look up
     * @return The name, or "Unknown" for a type no class answers for
     */
    static std::string_view displayName(AssetType type);

    /**
     * @brief The class whose files carry a magic
     * @param magic The four character magic read out of a file header
     * @return The class, or nullptr if no class writes that magic
     */
    static const AssetClass *findByFileMagic(uint32_t magic);

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
