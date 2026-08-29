#include "AssetRegistry.h"

#include "AssetImporter.h"
#include "assets/materials/AMaterial.h"
#include "assets/materials/AMaterialInstance.h"
#include "assets/meshes/ASkeletalMesh.h"
#include "assets/meshes/AStaticMesh.h"
#include "assets/modules/AModule.h"
#include "assets/shaders/AShader.h"
#include "assets/skeletons/ASkeleton.h"
#include "assets/textures/ATexture.h"
#include "assets/worlds/AWorld.h"
#include "core/utils/rp_assert.h"

#include <array>

namespace Rapture {

/**
 * @brief Builds a wrapper of class T around a payload, dropping a payload that could not be read
 */
template <typename T, typename P>
static std::unique_ptr<Asset> s_wrap(std::unique_ptr<P> payload)
{
    if (payload == nullptr) {
        return nullptr;
    }
    return std::make_unique<T>(std::move(payload));
}

static constexpr std::array<AssetClass, ASSET_TYPE_COUNT> ASSET_CLASSES = {{
    {},

    {.assetType = ASSET_TEXTURE,
     .fileMagic = Asset_fourCC("TEX "),
     .fileExtension = ".rtex",
     .displayName = "Texture",
     .deserialize = [](std::span<const uint8_t> payload) { return s_wrap<ATexture>(Texture::deserialize(payload)); },
     .import = Asset_importTexture},

    {.assetType = ASSET_CUBEMAP,
     .fileMagic = Asset_fourCC("CUBE"),
     .fileExtension = ".rtex",
     .displayName = "Cubemap",
     .import = Asset_importCubemap},

    {.assetType = ASSET_SHADER,
     .fileMagic = Asset_fourCC("SHDR"),
     .fileExtension = ".rshader",
     .displayName = "Shader",
     .import = Asset_importShader},

    {.assetType = ASSET_MATERIAL,
     .fileMagic = Asset_fourCC("MTL "),
     .fileExtension = ".rmat",
     .displayName = "Material",
     .deserialize = [](std::span<const uint8_t> payload) { return s_wrap<AMaterial>(BaseMaterial::deserialize(payload)); }},

    {.assetType = ASSET_MATERIAL_INSTANCE,
     .fileMagic = Asset_fourCC("MTLI"),
     .fileExtension = ".rmat",
     .displayName = "Material Instance",
     .deserialize =
         [](std::span<const uint8_t> payload) { return s_wrap<AMaterialInstance>(MaterialInstance::deserialize(payload)); },
     .import = Asset_importMaterialInstance},

    {.assetType = ASSET_STATIC_MESH,
     .fileMagic = Asset_fourCC("MESH"),
     .fileExtension = ".rmesh",
     .displayName = "Static Mesh",
     .deserialize = [](std::span<const uint8_t> payload) -> std::unique_ptr<Asset> { return AStaticMesh::deserialize(payload); }},

    {.assetType = ASSET_SKELETAL_MESH,
     .fileMagic = Asset_fourCC("SKMH"),
     .fileExtension = ".rmesh",
     .displayName = "Skeletal Mesh",
     .deserialize = [](std::span<const uint8_t> payload) -> std::unique_ptr<Asset> { return ASkeletalMesh::deserialize(payload); }},

    {.assetType = ASSET_MODULE,
     .fileMagic = Asset_fourCC("MODL"),
     .fileExtension = ".rmod",
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
     .fileMagic = Asset_fourCC("SKEL"),
     .fileExtension = ".rskel",
     .displayName = "Skeleton",
     .deserialize = [](std::span<const uint8_t> payload) -> std::unique_ptr<Asset> { return ASkeleton::deserialize(payload); }},

    {.assetType = ASSET_ANIMATION, .fileMagic = Asset_fourCC("ANIM"), .fileExtension = ".ranim", .displayName = "Animation"},

    {.assetType = ASSET_AUDIO, .fileMagic = Asset_fourCC("AUD "), .fileExtension = ".raudio", .displayName = "Audio"},

    {.assetType = ASSET_VIDEO, .fileMagic = Asset_fourCC("VID "), .fileExtension = ".rvideo", .displayName = "Video"},

    {.assetType = ASSET_WORLD,
     .fileMagic = Asset_fourCC("WRLD"),
     .fileExtension = ".rworld",
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

const AssetClass *AssetRegistry::findByFileMagic(uint32_t magic)
{
    for (const AssetClass &entry : ASSET_CLASSES) {
        if (entry.assetType != ASSET_NONE && entry.fileMagic == magic) {
            return &entry;
        }
    }
    return nullptr;
}

const AssetClass *AssetRegistry::findByExtension(std::string_view extension)
{
    for (const AssetClass &entry : ASSET_CLASSES) {
        if (entry.assetType != ASSET_NONE && entry.fileExtension == extension) {
            return &entry;
        }
    }
    return nullptr;
}

uint32_t AssetRegistry::fileMagic(AssetType type)
{
    const AssetClass *entry = find(type);
    RP_ASSERT(entry != nullptr, "asset type {} has no magic to be written as", static_cast<int>(type));
    return entry != nullptr ? entry->fileMagic : 0;
}

std::string_view AssetRegistry::fileExtension(AssetType type)
{
    const AssetClass *entry = find(type);
    RP_ASSERT(entry != nullptr, "asset type {} has no extension to be written with", static_cast<int>(type));
    return entry != nullptr ? entry->fileExtension : std::string_view{};
}

std::string_view AssetRegistry::displayName(AssetType type)
{
    const AssetClass *entry = find(type);
    return entry != nullptr ? entry->displayName : "Unknown";
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
