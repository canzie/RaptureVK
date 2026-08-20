#include "AssetManagerEditor.h"
#include "AssetCodec.h"
#include "AssetImporter.h"
#include "ReservedAssets.h"
#include "assets/asset_manager/Asset.h"
#include "assets/asset_manager/AssetCommon.h"
#include "core/utils/Log.h"
#include "assets/materials/Material.h"
#include "assets/materials/MaterialInstance.h"
#include "assets/meshes/MeshPrimitives.h"
#include "scene/instances/Instance.h"
#include "scene/instances/InstanceRegistry.h"
#include "gpu/textures/Texture.h"
#include "core/utils/UUID.h"
#include "app/Application.h"
#include "core/utils/Telemetry.h"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace Rapture {

static constexpr int32_t EVICTION_PRIORITY_LAZY = 1000; // EVICT_HINT_LAZY, drained before LAST
static constexpr int32_t EVICTION_PRIORITY_LAST = 0;    // EVICT_HINT_LAST, drained only under hard pressure

static constexpr double COLD_DRAIN_SOFT = 0.80;
static constexpr double COLD_DRAIN_STOP = 0.75;
static constexpr double COLD_DRAIN_HARD = 0.90;
static constexpr uint32_t COLD_DRAIN_CAP_SOFT = 8;
static constexpr uint32_t COLD_DRAIN_CAP_HARD = 64;

static constexpr float PRIMITIVE_SPHERE_RADIUS = 1.0f;
static constexpr uint32_t PRIMITIVE_SPHERE_SEGMENTS = 32;

static uint64_t s_hashPath(const std::filesystem::path &path)
{
    return std::hash<std::string>{}(path.lexically_normal().generic_string());
}

static uint64_t s_assetSizeHint(const Asset &asset)
{
    if (const AStaticMesh *mesh = asset.getUnderlyingAsset<AStaticMesh>()) {
        return mesh->geometry().getSizeBytes();
    }
    if (const ASkeletalMesh *mesh = asset.getUnderlyingAsset<ASkeletalMesh>()) {
        return mesh->geometry().getSizeBytes();
    }
    if (const Texture *texture = asset.getUnderlyingAsset<Texture>()) {
        return texture->getSizeBytes();
    }
    return 0;
}

static int32_t s_evictionPriority(const AssetMetadata &metadata)
{
    return metadata.evictionPolicy == AssetEvictionPolicy::EVICT_HINT_LAST ? EVICTION_PRIORITY_LAST : EVICTION_PRIORITY_LAZY;
}

static std::vector<uint8_t> s_serializeAsset(Asset &asset, const AssetMetadata &metadata)
{
    switch (metadata.assetType) {
    case ASSET_TEXTURE:
        if (Texture *texture = asset.getUnderlyingAsset<Texture>()) {
            return texture->serialize(metadata.getSourcePath().string());
        }
        break;
    case ASSET_SCENE_OBJECT:
        if (SerialDocument *document = asset.getUnderlyingAsset<SerialDocument>()) {
            std::string text = document->toText();
            return std::vector<uint8_t>(text.begin(), text.end());
        }
        break;
    case ASSET_MATERIAL:
        if (BaseMaterial *material = asset.getUnderlyingAsset<BaseMaterial>()) {
            return material->serialize();
        }
        break;
    case ASSET_MATERIAL_INSTANCE:
        if (MaterialInstance *instance = asset.getUnderlyingAsset<MaterialInstance>()) {
            return instance->serialize();
        }
        break;
    case ASSET_WORLD:
        if (World *world = asset.getUnderlyingAsset<World>()) {
            return world->serialize();
        }
        break;
    case ASSET_SKELETON:
        if (ASkeleton *skeleton = asset.getUnderlyingAsset<ASkeleton>()) {
            return skeleton->serialize();
        }
        break;
    case ASSET_STATIC_MESH:
        if (AStaticMesh *mesh = asset.getUnderlyingAsset<AStaticMesh>()) {
            return mesh->serialize();
        }
        break;
    case ASSET_SKELETAL_MESH:
        if (ASkeletalMesh *mesh = asset.getUnderlyingAsset<ASkeletalMesh>()) {
            return mesh->serialize();
        }
        break;
    default:
        break;
    }
    return {};
}

static bool s_deserializeAsset(Asset &asset, const AssetMetadata &metadata, std::span<const uint8_t> payload)
{
    switch (metadata.assetType) {
    case ASSET_TEXTURE:
        if (auto texture = Texture::deserialize(payload)) {
            asset.setAssetVariant(std::move(texture));
            return true;
        }
        break;
    case ASSET_STATIC_MESH:
        if (auto mesh = AStaticMesh::deserialize(payload)) {
            asset.setAssetVariant(std::move(mesh));
            return true;
        }
        break;
    case ASSET_SKELETAL_MESH:
        if (auto mesh = ASkeletalMesh::deserialize(payload)) {
            asset.setAssetVariant(std::move(mesh));
            return true;
        }
        break;
    case ASSET_SCENE_OBJECT: {
        std::string_view text(reinterpret_cast<const char *>(payload.data()), payload.size());
        auto document = std::make_unique<SerialDocument>(SerialDocument::parse(text));
        if (document->rootView().valid()) {
            asset.setAssetVariant(std::move(document));
            return true;
        }
        break;
    }
    case ASSET_MATERIAL:
        if (auto material = BaseMaterial::deserialize(payload)) {
            asset.setAssetVariant(std::move(material));
            return true;
        }
        break;
    case ASSET_MATERIAL_INSTANCE:
        if (auto instance = MaterialInstance::deserialize(payload)) {
            asset.setAssetVariant(std::move(instance));
            return true;
        }
        break;
    case ASSET_WORLD:
        if (auto world = World::deserialize(payload)) {
            asset.setAssetVariant(std::move(world));
            return true;
        }
        break;
    case ASSET_SKELETON:
        if (auto skeleton = ASkeleton::deserialize(payload)) {
            asset.setAssetVariant(std::move(skeleton));
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

static AssetVariant s_buildImportData(AssetImportDataVariant &data, std::vector<uint8_t> &payload, AssetType &type)
{
    if (auto *meshData = std::get_if<StaticMeshImportData>(&data)) {
        // the source bytes are already in hand here, so this skips the readback serialize does
        payload = AStaticMesh::serializeParams(meshData->params, meshData->defaultMaterial);
        type = ASSET_STATIC_MESH;
        return std::make_unique<AStaticMesh>(meshData->params, meshData->defaultMaterial);
    }
    if (auto *skeletalData = std::get_if<SkeletalMeshImportData>(&data)) {
        payload = ASkeletalMesh::serializeParams(skeletalData->params, skeletalData->skeleton,
                                                 skeletalData->inverseBindMatrices, skeletalData->defaultMaterial);
        type = ASSET_SKELETAL_MESH;
        return std::make_unique<ASkeletalMesh>(skeletalData->params, skeletalData->skeleton,
                                               std::move(skeletalData->inverseBindMatrices), skeletalData->defaultMaterial);
    }
    if (auto *sceneObjectData = std::get_if<SceneObjectImportData>(&data)) {
        std::string text = sceneObjectData->document->toText();
        payload.assign(text.begin(), text.end());
        type = ASSET_SCENE_OBJECT;
        return std::move(sceneObjectData->document);
    }
    if (auto *materialData = std::get_if<BaseMaterialImportData>(&data)) {
        payload = materialData->material->serialize();
        type = ASSET_MATERIAL;
        return std::move(materialData->material);
    }
    if (auto *instanceData = std::get_if<MaterialInstanceImportData>(&data)) {
        payload = instanceData->instance->serialize();
        type = ASSET_MATERIAL_INSTANCE;
        return std::move(instanceData->instance);
    }
    if (auto *worldData = std::get_if<WorldImportData>(&data)) {
        payload = worldData->world->serialize();
        type = ASSET_WORLD;
        return std::move(worldData->world);
    }
    if (auto *skeletonData = std::get_if<SkeletonImportData>(&data)) {
        auto skeleton = std::make_unique<ASkeleton>(std::move(skeletonData->skeleton));
        payload = skeleton->serialize();
        type = ASSET_SKELETON;
        return skeleton;
    }

    type = ASSET_NONE;
    return std::monostate{};
}

AssetManagerEditor::AssetManagerEditor(const Telemetry *telemetry) : AssetManagerBase(), m_telemetry(telemetry) {}

AssetManagerEditor::~AssetManagerEditor()
{
    m_shuttingDown = true;
    // The pending writes hold refs into the assets and their metadata, so they have to go first
    m_pendingWrites.clear();
    m_deferredFrees.clear();
    m_assets.clear();
    m_defaultAssetHandles.clear();
}

bool AssetManagerEditor::isAssetHandleValid(AssetHandle handle) const
{
    return m_assets.find(handle) != nullptr;
}

bool AssetManagerEditor::isAssetLoaded(AssetHandle handle) const
{
    const AssetSlot *slot = m_assets.find(handle);
    return slot != nullptr && slot->isLoaded();
}

Asset &AssetManagerEditor::getAsset(AssetHandle handle)
{
    AssetSlot *slot = m_assets.find(handle);
    if (slot == nullptr) {
        RP_CORE_ERROR("Invalid asset handle {}, it is not registered", handle);
        return Asset::null;
    }

    if (slot->isLoaded()) {
        return *slot->asset;
    }

    AssetMetadata &metadata = *slot->metadata;
    if (!metadata) {
        RP_CORE_ERROR("Invalid asset metadata, import asset first");
        return Asset::null;
    }

    std::unique_ptr<Asset> asset = loadFromMetadata(handle, metadata);
    if (!asset) {
        RP_CORE_ERROR("Failed to load asset '{}'", metadata.getName());
        return Asset::null;
    }

    slot->asset = std::move(asset);
    metadata.sizeHintBytes = s_assetSizeHint(*slot->asset);
    return *slot->asset;
}

AssetMetadata &AssetManagerEditor::getAssetMetadata(AssetHandle handle)
{
    AssetSlot *slot = m_assets.find(handle);
    if (slot != nullptr) {
        return *slot->metadata;
    }
    return AssetMetadata::null;
}

const AssetMetadata &AssetManagerEditor::getAssetMetadata(AssetHandle handle) const
{
    const AssetSlot *slot = m_assets.find(handle);
    if (slot != nullptr) {
        return *slot->metadata;
    }
    return AssetMetadata::const_null;
}

Asset &AssetManagerEditor::importAsset(const AssetImportFileRequest &request)
{
    if (request.source.empty()) {
        RP_CORE_ERROR("Import source path is empty");
        return Asset::null;
    }

    if (request.source.extension() == ".rasset") {
        RP_CORE_ERROR("importAsset expects an external file, use registerRaptureAsset for '{}'", request.source.string());
        return Asset::null;
    }

    AssetType assetType = determineAssetType(request.source.string());

    // A reimport of the same source with the same config lands on the same type, so only that bucket can hold it
    for (const AssetSlot &slot : m_assets.ofType(assetType)) {
        if (slot.metadata->getSourcePath() == request.source && slot.metadata->importConfig == request.config) {
            return getAsset(slot.handle);
        }
    }

    auto metadata = std::make_unique<AssetMetadata>();
    metadata->storageType = AssetStorageType::DISK;
    metadata->provenance = AssetProvenance{.sourcePath = request.source};
    metadata->name = request.name.empty() ? request.source.stem().string() : request.name;
    metadata->assetType = assetType;
    metadata->importConfig = request.config;

    AssetHandle handle = UUIDGenerator::Generate();
    auto asset = std::make_unique<Asset>(handle);
    if (!AssetImporter::importAsset(*asset, *metadata)) {
        RP_CORE_ERROR("Failed to import asset: {}", request.source.string());
        return Asset::null;
    }

    // The load is asynchronous, so the empty payload defers the .rasset write until it finishes
    return registerImportedAsset(handle, std::move(asset), std::move(metadata), request.output, {});
}

Asset &AssetManagerEditor::importAsset(AssetImportDataRequest request)
{
    std::vector<uint8_t> payload;
    AssetType type = ASSET_NONE;
    AssetVariant value = s_buildImportData(request.data, payload, type);
    if (type == ASSET_NONE) {
        RP_CORE_ERROR("Unsupported asset import data for '{}'", request.name);
        return Asset::null;
    }

    AssetHandle handle = UUIDGenerator::Generate();
    auto asset = std::make_unique<Asset>(std::move(value), handle);
    asset->status = AssetStatus::LOADED;

    auto metadata = std::make_unique<AssetMetadata>();
    metadata->assetType = type;
    metadata->storageType = AssetStorageType::DISK;
    metadata->name = request.name;
    metadata->provenance = std::move(request.provenance);

    // recorded on the metadata so an asset can be filtered by class while its payload is evicted
    if (SerialDocument *document = asset->getUnderlyingAsset<SerialDocument>()) {
        metadata->authoredClass = InstanceRegistry::find(Instance::readClassName(document->rootView()));
    }

    return registerImportedAsset(handle, std::move(asset), std::move(metadata), request.output, payload);
}

AssetHandle AssetManagerEditor::registerRaptureAsset(std::filesystem::path path)
{
    if (path.extension() != ".rasset") {
        RP_CORE_ERROR("registerRaptureAsset expects a .rasset file: {}", path.string());
        return INVALID_ASSET_HANDLE;
    }

    auto info = AssetCodec::readRaptureAssetInfo(path);
    if (!info || !info->metadata) {
        RP_CORE_ERROR("Failed to read .rasset header: {}", path.string());
        return INVALID_ASSET_HANDLE;
    }

    AssetHandle handle = info->uuid;

    AssetSlot *existing = m_assets.find(handle);
    if (existing == nullptr) {
        info->metadata->assetPath = path;
        m_assets.insert(handle, std::move(info->metadata));
    } else {
        existing->metadata->assetPath = path;
    }
    m_pathIndex[s_hashPath(path)] = handle;

    return handle;
}

uint32_t AssetManagerEditor::registerAssetDirectory(const std::filesystem::path &directory)
{
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) {
        return 0;
    }

    uint32_t registered = 0;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".rasset") {
            continue;
        }
        if (registerRaptureAsset(it->path()) != INVALID_ASSET_HANDLE) {
            ++registered;
        }
    }

    RP_CORE_INFO("Registered {} assets from '{}'", registered, directory.string());
    return registered;
}

Asset &AssetManagerEditor::importDefaultAsset(AssetType assetType)
{
    auto it = m_defaultAssetHandles.find(assetType);
    if (it != m_defaultAssetHandles.end()) {
        AssetHandle existingHandle = it->second;
        // Verify the asset is still loaded and valid
        if (isAssetLoaded(existingHandle)) {
            return getAsset(existingHandle);
        } else {
            // Asset was unloaded somehow, remove the handle and recreate
            RP_CORE_WARN("Default {} asset was unloaded, recreating", AssetTypeToString(assetType));
            m_defaultAssetHandles.erase(it);
        }
    }

    switch (assetType) {
    case ASSET_TEXTURE: {
        auto defaultTexture = Texture::createDefaultWhiteTexture();
        if (!defaultTexture) {
            RP_CORE_ERROR("Failed to create default white texture");
            return Asset::null;
        }

        Asset &asset =
            registerReservedAsset(RE_WHITE_TEXTURE, std::move(defaultTexture), "<default_white_texture>", ASSET_TEXTURE);
        if (asset) {
            m_defaultAssetHandles[assetType] = asset.getHandle();
        }
        return asset;
    }
    case ASSET_MATERIAL_INSTANCE: {
        auto baseMaterial = MaterialManager::getMaterial("Default Material");
        if (!baseMaterial) {
            RP_CORE_ERROR("Failed to get default material");
            return Asset::null;
        }

        auto defaultMaterial = std::make_unique<MaterialInstance>(baseMaterial, "Default");
        Asset &asset = registerReservedAsset(RE_DEFAULT_MATERIAL_INSTANCE, std::move(defaultMaterial), "<default_material>",
                                             ASSET_MATERIAL_INSTANCE);
        if (asset) {
            m_defaultAssetHandles[assetType] = asset.getHandle();
        }
        return asset;
    }
    default:
        RP_CORE_WARN("Default asset type {} not implemented", AssetTypeToString(assetType));
        return Asset::null;
    }
}

AssetType AssetManagerEditor::determineAssetType(const std::string &path)
{
    std::filesystem::path filePath(path);
    std::string extension = filePath.extension().string();

    for (char &c : extension) {
        c = std::tolower(c);
    }

    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp" ||
        extension == ".hdr") {
        return ASSET_TEXTURE;
    } else if (extension == ".cubemap") {
        return ASSET_CUBEMAP;
    } else if (extension == ".gltf" || extension == ".glb" || extension == ".fbx") {
        return ASSET_SCENE_OBJECT;
    } else if (extension == ".rmat") {
        return ASSET_MATERIAL;
    } else if (extension == ".spv" || extension == ".glsl") {
        return ASSET_SHADER;
    }

    RP_CORE_WARN("Unknown asset type for extension: {}", extension);
    return ASSET_NONE;
}

Asset &AssetManagerEditor::registerVirtualAsset(AssetVariant assetValue, const std::string &virtualName, AssetType assetType)
{
    if (std::holds_alternative<std::monostate>(assetValue)) {
        RP_CORE_ERROR("Asset variant is empty");
        return Asset::null;
    }

    if (virtualName.empty()) {
        RP_CORE_ERROR("Virtual name cannot be empty");
        return Asset::null;
    }

    for (const AssetSlot &slot : m_assets.ofType(assetType)) {
        if (slot.metadata->isVirtualAsset() && slot.metadata->name == virtualName) {
            RP_CORE_WARN("Virtual asset with name '{}' already exists", virtualName);
            return Asset::null;
        }
    }

    AssetHandle handle = UUIDGenerator::Generate();
    auto asset = std::make_unique<Asset>(std::move(assetValue), handle);
    asset->status = AssetStatus::LOADED; // Virtual assets are immediately loaded

    auto metadata = std::make_unique<AssetMetadata>();
    metadata->assetType = assetType;
    metadata->storageType = AssetStorageType::VIRTUAL;
    metadata->name = virtualName;
    metadata->sizeHintBytes = s_assetSizeHint(*asset);

    AssetSlot &slot = m_assets.insert(handle, std::move(metadata));
    slot.asset = std::move(asset);

    RP_CORE_INFO("Registered virtual {} asset: '{}'", AssetTypeToString(assetType), virtualName);
    return *slot.asset;
}

Asset &AssetManagerEditor::registerReservedAsset(AssetHandle handle, AssetVariant assetValue, const std::string &name,
                                                 AssetType assetType)
{
    if (!Asset_isReserved(handle)) {
        RP_CORE_ERROR("registerReservedAsset called with a non-reserved handle {}", handle);
        return Asset::null;
    }

    if (std::holds_alternative<std::monostate>(assetValue)) {
        RP_CORE_ERROR("Asset variant is empty");
        return Asset::null;
    }

    if (AssetSlot *existing = m_assets.find(handle); existing != nullptr && existing->asset != nullptr) {
        return *existing->asset;
    }

    auto asset = std::make_unique<Asset>(std::move(assetValue), handle);
    asset->status = AssetStatus::LOADED;

    auto metadata = std::make_unique<AssetMetadata>();
    metadata->assetType = assetType;
    metadata->storageType = AssetStorageType::VIRTUAL;
    metadata->name = name;
    metadata->sizeHintBytes = s_assetSizeHint(*asset);

    AssetSlot &slot = m_assets.insert(handle, std::move(metadata));
    slot.asset = std::move(asset);
    return *slot.asset;
}

std::unique_ptr<Asset> AssetManagerEditor::loadFromMetadata(AssetHandle handle, AssetMetadata &metadata)
{
    auto asset = std::make_unique<Asset>(handle);

    if (!metadata.assetPath.empty() && std::filesystem::exists(metadata.assetPath)) {
        std::vector<uint8_t> payload = AssetCodec::readRaptureAssetPayload(metadata.assetPath);
        if (!payload.empty() && s_deserializeAsset(*asset, metadata, payload)) {
            asset->status = AssetStatus::LOADED;
            return asset;
        }
        RP_CORE_WARN("Failed to load '{}' from its .rasset, falling back to the source file", metadata.getName());
    }

    if (!AssetImporter::importAsset(*asset, metadata)) {
        return nullptr;
    }
    return asset;
}

Asset &AssetManagerEditor::registerImportedAsset(AssetHandle handle, std::unique_ptr<Asset> asset,
                                                 std::unique_ptr<AssetMetadata> metadata, const std::filesystem::path &outputFolder,
                                                 std::span<const uint8_t> payload)
{
    metadata->sizeHintBytes = s_assetSizeHint(*asset);

    bool deferWrite = false;
    if (outputFolder.empty()) {
        // Shaders keep their native format, everything else is expected to own a .rasset
        if (metadata->assetType != ASSET_SHADER) {
            RP_CORE_ERROR("No output folder provided for '{}'", metadata->name);
        }
    } else if (!payload.empty()) {
        writeRaptureAssetFile(handle, outputFolder, *metadata, payload);
    } else {
        deferWrite = true;
    }

    AssetSlot &slot = m_assets.insert(handle, std::move(metadata));
    slot.asset = std::move(asset);

    if (deferWrite) {
        m_pendingWrites.push_back({AssetRef(slot.asset.get(), &slot.metadata->useCount), outputFolder});
    }

    return *slot.asset;
}

bool AssetManagerEditor::unregisterVirtualAsset(AssetHandle handle)
{
    AssetSlot *slot = m_assets.find(handle);
    if (slot == nullptr) {
        RP_CORE_WARN("Asset handle not found in registry");
        return false;
    }

    if (!slot->metadata->isVirtualAsset()) {
        RP_CORE_ERROR("Cannot unregister non-virtual asset: {}", slot->metadata->name);
        return false;
    }

    // The name is read after the erase, so it cannot stay a reference into the metadata being dropped
    std::string name = slot->metadata->name;
    m_assets.erase(handle);

    RP_CORE_INFO("Unregistered virtual asset: '{}'", name);
    return true;
}

void AssetManagerEditor::registerBuiltinAssets()
{
    registerReservedAsset(RE_PRIMITIVE_CUBE_MESH, std::make_unique<AStaticMesh>(Primitives::CreateCube()), "<cube>",
                          ASSET_STATIC_MESH);
    registerReservedAsset(
        RE_PRIMITIVE_SPHERE_MESH,
        std::make_unique<AStaticMesh>(Primitives::CreateSphere(PRIMITIVE_SPHERE_RADIUS, PRIMITIVE_SPHERE_SEGMENTS)), "<sphere>",
        ASSET_STATIC_MESH);
    registerReservedAsset(RE_PRIMITIVE_PLANE_MESH, std::make_unique<AStaticMesh>(Primitives::CreatePlane()), "<plane>",
                          ASSET_STATIC_MESH);

    importDefaultAsset(ASSET_TEXTURE);
    importDefaultAsset(ASSET_MATERIAL_INSTANCE);

    auto gridMaterial = MaterialManager::getMaterial("Grid Material");
    if (gridMaterial) {
        registerReservedAsset(RE_GRID_MATERIAL_INSTANCE, std::make_unique<MaterialInstance>(gridMaterial, "Grid"), "<grid>",
                              ASSET_MATERIAL_INSTANCE);
    } else {
        RP_CORE_ERROR("grid material was not created at startup, so it has no instance to draw with");
    }
}

Asset &AssetManagerEditor::getVirtualAssetByName(const std::string &virtualName)
{
    for (int type = 0; type < ASSET_TYPE_COUNT; ++type) {
        for (const AssetSlot &slot : m_assets.ofType(static_cast<AssetType>(type))) {
            if (slot.metadata->isVirtualAsset() && slot.metadata->name == virtualName) {
                return getAsset(slot.handle);
            }
        }
    }
    return Asset::null;
}

std::vector<AssetHandle> AssetManagerEditor::getVirtualAssetsByType(AssetType type) const
{
    std::vector<AssetHandle> result;
    for (const AssetSlot &slot : m_assets.ofType(type)) {
        if (slot.metadata->isVirtualAsset()) {
            result.push_back(slot.handle);
        }
    }
    return result;
}

AssetHandle AssetManagerEditor::findAssetByPath(const std::filesystem::path &path) const
{
    auto it = m_pathIndex.find(s_hashPath(path));
    if (it == m_pathIndex.end()) {
        return INVALID_ASSET_HANDLE;
    }

    // Guard against the rare hash collision by confirming the stored path matches
    const AssetMetadata &metadata = getAssetMetadata(it->second);
    if (metadata.assetPath.lexically_normal() != path.lexically_normal()) {
        return INVALID_ASSET_HANDLE;
    }
    return it->second;
}

void AssetManagerEditor::ensureDeferredFreeBuckets()
{
    uint32_t framesInFlight = Application::getInstance().getFramesInFlight();
    size_t bucketCount = static_cast<size_t>(framesInFlight) + 1;
    if (m_deferredFrees.size() < bucketCount) {
        m_deferredFrees.resize(bucketCount);
    }
}

void AssetManagerEditor::onUpdate()
{
    ensureDeferredFreeBuckets();

    m_deferredFreeBucket = (m_deferredFreeBucket + 1) % m_deferredFrees.size();

    std::vector<std::unique_ptr<Asset>> toFree = std::move(m_deferredFrees[m_deferredFreeBucket]);
    m_deferredFrees[m_deferredFreeBucket].clear();

    processUnloadRequests();
    processPendingWrites();
    drainColdList();

    toFree.clear();
}

void AssetManagerEditor::processPendingWrites()
{
    for (size_t i = 0; i < m_pendingWrites.size();) {
        Asset *asset = m_pendingWrites[i].asset.get();

        bool finished = true;
        if (asset != nullptr) {
            AssetHandle handle = asset->getHandle();
            AssetMetadata &metadata = getAssetMetadata(handle);
            AssetStatus status = asset->getStatus();
            if (status == AssetStatus::LOADED) {
                writeRaptureAssetFile(handle, m_pendingWrites[i].outputFolder, metadata, s_serializeAsset(*asset, metadata));
            } else if (status == AssetStatus::FAILED) {
                RP_CORE_WARN("Dropping the deferred .rasset write for '{}' ({}), its load failed", metadata.getName(),
                             AssetTypeToString(metadata.assetType));
            } else {
                finished = false;
            }
        }

        if (finished) {
            m_pendingWrites[i] = std::move(m_pendingWrites.back());
            m_pendingWrites.pop_back();
        } else {
            ++i;
        }
    }
}

bool AssetManagerEditor::updateAsset(AssetHandle handle, AssetVariant asset)
{
    AssetSlot *slot = m_assets.find(handle);
    if (slot == nullptr) {
        RP_CORE_ERROR("cannot update unregistered asset {}", handle);
        return false;
    }

    AssetMetadata &metadata = *slot->metadata;
    if (metadata.assetPath.empty()) {
        RP_CORE_ERROR("'{}' has no file to update", metadata.getName());
        return false;
    }

    if (slot->asset == nullptr) {
        slot->asset = std::make_unique<Asset>(handle);
    }

    Asset &loadedAsset = *slot->asset;
    loadedAsset.setAssetVariant(std::move(asset));
    loadedAsset.status = AssetStatus::LOADED;

    std::vector<uint8_t> payload = s_serializeAsset(loadedAsset, metadata);
    if (payload.empty()) {
        RP_CORE_ERROR("Failed to serialize '{}'", metadata.getName());
        return false;
    }

    if (!AssetCodec::writeRaptureAsset(metadata.assetPath, handle, metadata, payload)) {
        RP_CORE_ERROR("Failed to write .rasset for '{}'", metadata.getName());
        return false;
    }

    return true;
}

bool AssetManagerEditor::saveAsset(AssetHandle handle, const std::filesystem::path &folder)
{
    AssetSlot *slot = m_assets.find(handle);
    if (slot == nullptr || slot->asset == nullptr) {
        RP_CORE_ERROR("cannot save asset {}, it holds nothing", handle);
        return false;
    }

    AssetMetadata &metadata = *slot->metadata;
    std::vector<uint8_t> payload = s_serializeAsset(*slot->asset, metadata);
    if (payload.empty()) {
        RP_CORE_ERROR("Failed to serialize '{}'", metadata.getName());
        return false;
    }

    if (metadata.assetPath.empty()) {
        metadata.storageType = AssetStorageType::DISK;
        writeRaptureAssetFile(handle, folder, metadata, payload);
        return !metadata.assetPath.empty();
    }

    if (!AssetCodec::writeRaptureAsset(metadata.assetPath, handle, metadata, payload)) {
        RP_CORE_ERROR("Failed to write .rasset for '{}'", metadata.getName());
        return false;
    }
    return true;
}

void AssetManagerEditor::writeRaptureAssetFile(AssetHandle handle, const std::filesystem::path &folder,
                                               AssetMetadata &metadata, std::span<const uint8_t> payload)
{
    if (payload.empty()) {
        RP_CORE_ERROR("Failed to serialize '{}'", metadata.getName());
        return;
    }

    // The display name keeps its spaces, the on-disk file name does not
    std::string fileName = metadata.getName();
    std::replace(fileName.begin(), fileName.end(), ' ', '_');
    std::filesystem::path output = folder / (fileName + ".rasset");
    if (AssetCodec::writeRaptureAsset(output, handle, metadata, payload)) {
        metadata.assetPath = output;
        m_pathIndex[s_hashPath(output)] = handle;
    } else {
        RP_CORE_ERROR("Failed to write .rasset for '{}'", metadata.getName());
    }
}

void AssetManagerEditor::processUnloadRequests()
{
    std::vector<AssetHandle> stillLoading;
    AssetHandle handle;
    while (m_pendingUnloadChecks.try_dequeue(handle)) {
        const AssetSlot *slot = m_assets.find(handle);
        if (slot == nullptr) {
            continue;
        }

        if (slot->asset != nullptr) {
            AssetStatus status = slot->asset->getStatus();
            if (status == AssetStatus::REQUESTED || status == AssetStatus::LOADING) {
                stillLoading.push_back(handle);
                continue;
            }
        }

        const AssetMetadata &metadata = *slot->metadata;
        if (metadata.useCount.load(std::memory_order_acquire) != 0) {
            continue;
        }

        if (metadata.evictionPolicy == AssetEvictionPolicy::EVICT_IMMEDIATE) {
            evictAsset(handle);
        } else {
            addToColdList(handle, metadata);
        }
    }

    for (AssetHandle h : stillLoading) {
        m_pendingUnloadChecks.enqueue(h);
    }
}

void AssetManagerEditor::addToColdList(AssetHandle handle, const AssetMetadata &metadata)
{
    m_coldList.push(handle, s_evictionPriority(metadata));
}

void AssetManagerEditor::drainColdList()
{
    if (m_telemetry == nullptr || m_telemetry->vramBudgetBytes == 0 || m_coldList.empty()) {
        return;
    }

    double usage = static_cast<double>(m_telemetry->vramUsedBytes) / static_cast<double>(m_telemetry->vramBudgetBytes);
    if (usage < COLD_DRAIN_STOP) {
        m_coldDraining = false;
    } else if (usage > COLD_DRAIN_SOFT) {
        m_coldDraining = true;
    }

    bool hard = usage > COLD_DRAIN_HARD;
    if (!m_coldDraining && !hard) {
        return;
    }

    uint32_t cap = hard ? COLD_DRAIN_CAP_HARD : COLD_DRAIN_CAP_SOFT;
    uint32_t drained = 0;
    while (!m_coldList.empty() && drained < cap) {
        AssetHandle handle = m_coldList.front();
        const AssetMetadata &metadata = getAssetMetadata(handle);

        if (!hard && metadata.evictionPolicy == AssetEvictionPolicy::EVICT_HINT_LAST) {
            break;
        }

        m_coldList.pop();

        if (metadata.useCount.load(std::memory_order_acquire) != 0) {
            continue;
        }

        evictAsset(handle);
        ++drained;
    }
}

bool AssetManagerEditor::evictAsset(AssetHandle handle)
{
    AssetSlot *slot = m_assets.find(handle);
    if (slot == nullptr || slot->asset == nullptr) {
        return false;
    }

    // Engine builtins live under reserved handles and are never evicted
    if (Asset_isReserved(handle)) {
        return false;
    }

    const AssetMetadata &metadata = *slot->metadata;
    uint32_t refCount = metadata.useCount.load(std::memory_order_acquire);
    if (refCount != 0) {
        RP_CORE_WARN("Cannot unload asset({}) still in use, {} references remain", AssetTypeToString(metadata.assetType), refCount);
        return false;
    }

    RP_CORE_INFO("Evicting {} asset '{}'", AssetTypeToString(metadata.assetType), metadata.name);
    bool isVirtual = metadata.isVirtualAsset();

    ensureDeferredFreeBuckets();
    m_deferredFrees[m_deferredFreeBucket].push_back(std::move(slot->asset));

    // A virtual asset has no file to reload from, so dropping its payload drops its registration too
    if (isVirtual) {
        m_assets.erase(handle);
    }

    return true;
}

void AssetManagerEditor::requestUnload(AssetHandle handle)
{
    if (m_shuttingDown) {
        return;
    }
    m_pendingUnloadChecks.enqueue(handle);
}

} // namespace Rapture
