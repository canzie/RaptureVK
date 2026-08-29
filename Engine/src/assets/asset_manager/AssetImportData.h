#ifndef RAPTURE__ASSET_IMPORT_DATA_H
#define RAPTURE__ASSET_IMPORT_DATA_H

#include "AssetCommon.h"
#include "ReservedAssets.h"

#include "assets/materials/Material.h"
#include "assets/materials/MaterialInstance.h"
#include "assets/meshes/Mesh.h"
#include "assets/skeletons/Skeleton.h"
#include "core/serialization/SerialDocument.h"
#include "scene/World.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Rapture {

struct StaticMeshImportData {
    MeshAllocatorParams params;
    AssetHandle defaultMaterial = RE_DEFAULT_MATERIAL_INSTANCE;
};

struct SkeletalMeshImportData {
    MeshAllocatorParams params;
    AssetHandle skeleton = INVALID_ASSET_HANDLE;
    std::vector<glm::mat4> inverseBindMatrices;
    AssetHandle defaultMaterial = RE_DEFAULT_MATERIAL_INSTANCE;
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

using AssetImportDataVariant =
    std::variant<std::monostate, StaticMeshImportData, SkeletalMeshImportData, ModuleImportData, BaseMaterialImportData,
                 MaterialInstanceImportData, WorldImportData, SkeletonImportData>;

struct AssetImportDataRequest {
    AssetImportDataVariant data;
    std::filesystem::path output = {};
    std::string name = {};
    std::optional<AssetProvenance> provenance = std::nullopt;
};

} // namespace Rapture

#endif // RAPTURE__ASSET_IMPORT_DATA_H
