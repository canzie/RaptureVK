#include "AssetRegistry.h"

#include "assets/materials/AMaterial.h"
#include "assets/materials/AMaterialInstance.h"
#include "assets/meshes/ASkeletalMesh.h"
#include "assets/meshes/AStaticMesh.h"
#include "assets/modules/AModule.h"
#include "assets/shaders/AShader.h"
#include "assets/skeletons/ASkeleton.h"
#include "assets/textures/ATexture.h"
#include "assets/worlds/AWorld.h"

#include <array>

namespace Rapture {

/**
 * @brief Packs four characters into the code a class writes into its files
 * @param text Exactly four characters
 * @return The code
 */
static constexpr uint32_t s_fourCC(const char (&text)[5])
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(text[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(text[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(text[2])) << 8) | static_cast<uint32_t>(static_cast<uint8_t>(text[3]));
}

/**
 * @brief Builds a wrapper of class T around a payload, dropping a payload that could not be read
 */
template <typename T, typename P> static std::unique_ptr<Asset> s_wrap(std::unique_ptr<P> payload)
{
    if (payload == nullptr) {
        return nullptr;
    }
    return std::make_unique<T>(std::move(payload));
}

static constexpr std::array<AssetClass, ASSET_TYPE_COUNT> ASSET_CLASSES = {{
    {},

    {.assetType = ASSET_TEXTURE,
     .code = s_fourCC("TEX "),
     .extension = ".rtex",
     .displayName = "Texture",
     .deserialize = [](std::span<const uint8_t> payload) { return s_wrap<ATexture>(Texture::deserialize(payload)); }},

    {.assetType = ASSET_CUBEMAP, .code = s_fourCC("CUBE"), .extension = ".rtex", .displayName = "Cubemap"},

    {.assetType = ASSET_SHADER, .code = s_fourCC("SHDR"), .extension = ".rshader", .displayName = "Shader"},

    {.assetType = ASSET_MATERIAL,
     .code = s_fourCC("MTL "),
     .extension = ".rmat",
     .displayName = "Material",
     .deserialize = [](std::span<const uint8_t> payload) { return s_wrap<AMaterial>(BaseMaterial::deserialize(payload)); }},

    {.assetType = ASSET_MATERIAL_INSTANCE,
     .code = s_fourCC("MTLI"),
     .extension = ".rmat",
     .displayName = "Material Instance",
     .deserialize =
         [](std::span<const uint8_t> payload) { return s_wrap<AMaterialInstance>(MaterialInstance::deserialize(payload)); }},

    {.assetType = ASSET_STATIC_MESH,
     .code = s_fourCC("MESH"),
     .extension = ".rmesh",
     .displayName = "Static Mesh",
     .deserialize = [](std::span<const uint8_t> payload) -> std::unique_ptr<Asset> { return AStaticMesh::deserialize(payload); }},

    {.assetType = ASSET_SKELETAL_MESH,
     .code = s_fourCC("SKMH"),
     .extension = ".rmesh",
     .displayName = "Skeletal Mesh",
     .deserialize = [](std::span<const uint8_t> payload) -> std::unique_ptr<Asset> { return ASkeletalMesh::deserialize(payload); }},

    {.assetType = ASSET_MODULE,
     .code = s_fourCC("MODL"),
     .extension = ".rmod",
     .displayName = "Module",
     .deserialize = [](std::span<const uint8_t> payload) -> std::unique_ptr<Asset> {
         std::string_view text(reinterpret_cast<const char *>(payload.data()), payload.size());
         auto document = std::make_unique<SerialDocument>(SerialDocument::parse(text));
         if (!document->rootView().valid()) {
             return nullptr;
         }
         return std::make_unique<AModule>(std::move(document));
     }},

    {.assetType = ASSET_SKELETON,
     .code = s_fourCC("SKEL"),
     .extension = ".rskel",
     .displayName = "Skeleton",
     .deserialize = [](std::span<const uint8_t> payload) -> std::unique_ptr<Asset> { return ASkeleton::deserialize(payload); }},

    {.assetType = ASSET_ANIMATION, .code = s_fourCC("ANIM"), .extension = ".ranim", .displayName = "Animation"},

    {.assetType = ASSET_AUDIO, .code = s_fourCC("AUD "), .extension = ".raudio", .displayName = "Audio"},

    {.assetType = ASSET_VIDEO, .code = s_fourCC("VID "), .extension = ".rvideo", .displayName = "Video"},

    {.assetType = ASSET_WORLD,
     .code = s_fourCC("WRLD"),
     .extension = ".rworld",
     .displayName = "World",
     .deserialize = [](std::span<const uint8_t> payload) { return s_wrap<AWorld>(World::deserialize(payload)); }},
}};

// the table is read by indexing it with an AssetType, so an entry in the wrong slot would answer for another class
static consteval bool s_assetClassesAreInEnumOrder()
{
    for (int slot = ASSET_NONE + 1; slot < ASSET_TYPE_COUNT; slot++) {
        if (ASSET_CLASSES[slot].assetType != static_cast<AssetType>(slot)) {
            return false;
        }
    }
    return true;
}

static_assert(s_assetClassesAreInEnumOrder(), "ASSET_CLASSES needs one entry per AssetType, in the order the enum declares them");

const AssetClass *AssetRegistry::find(AssetType type)
{
    const AssetClass &entry = ASSET_CLASSES[type];
    return entry.assetType != ASSET_NONE ? &entry : nullptr;
}

const AssetClass *AssetRegistry::findByCode(uint32_t code)
{
    for (const AssetClass &entry : ASSET_CLASSES) {
        if (entry.assetType != ASSET_NONE && entry.code == code) {
            return &entry;
        }
    }
    return nullptr;
}

const AssetClass *AssetRegistry::findByExtension(std::string_view extension)
{
    for (const AssetClass &entry : ASSET_CLASSES) {
        if (entry.assetType != ASSET_NONE && entry.extension == extension) {
            return &entry;
        }
    }
    return nullptr;
}

bool AssetRegistry::isRaptureExtension(std::string_view extension)
{
    return findByExtension(extension) != nullptr;
}

std::span<const AssetClass> AssetRegistry::classes()
{
    return ASSET_CLASSES;
}

} // namespace Rapture
