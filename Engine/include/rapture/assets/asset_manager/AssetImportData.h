#ifndef RAPTURE__ASSET_IMPORT_DATA_H
#define RAPTURE__ASSET_IMPORT_DATA_H

#include <rapture/assets/asset_manager/AssetCommon.h>
#include <rapture/assets/asset_manager/ReservedAssets.h>

#include <rapture/assets/materials/Material.h>
#include <rapture/assets/materials/MaterialInstance.h>
#include <rapture/assets/meshes/Mesh.h>
#include <rapture/assets/skeletons/Skeleton.h>
#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/scene/World.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Rapture {

struct StaticMeshImportData {
    MeshAllocatorParams params;
    std::vector<AssetHandle> materialSlots;
};

struct SkeletalMeshImportData {
    MeshAllocatorParams params;
    AssetHandle skeleton = INVALID_ASSET_HANDLE;
    std::vector<glm::mat4> inverseBindMatrices;
    std::vector<AssetHandle> materialSlots;
};

struct ModuleImportData {
    std::unique_ptr<SerialDocument> document;
};

struct BaseMaterialImportData {
    std::unique_ptr<BaseMaterial> material;
};

struct MaterialInstanceImportData {
    std::unique_ptr<MaterialInstance> instance;
};

struct WorldImportData {
    std::unique_ptr<World> world;
};

struct SkeletonImportData {
    std::unique_ptr<Skeleton> skeleton;
};

struct ScriptImportData {
    std::string source;
    bool sourceIsInline = true;
};

using AssetImportDataVariant =
    std::variant<std::monostate, StaticMeshImportData, SkeletalMeshImportData, ModuleImportData, BaseMaterialImportData,
                 MaterialInstanceImportData, WorldImportData, SkeletonImportData, ScriptImportData>;

struct AssetImportDataRequest {
    AssetImportDataVariant data;
    std::filesystem::path output = {};
    std::string name = {};
    std::optional<AssetProvenance> provenance = std::nullopt;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_IMPORT_DATA_H
