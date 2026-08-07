#include "AssetManagerEditor.h"
#include "AssetCodec.h"
#include "AssetImporter.h"
#include "ReservedAssets.h"
#include "asset_manager/Asset.h"
#include "asset_manager/AssetCommon.h"
#include "logging/Log.h"
#include "materials/Material.h"
#include "materials/MaterialInstance.h"
#include "meshes/MeshPrimitives.h"
#include "textures/Texture.h"
#include "utils/UUID.h"
#include "window_context/Application.h"
#include "window_context/Telemetry.h"

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
    if (const Mesh *mesh = asset.getUnderlyingAsset<Mesh>()) {
        return mesh->getSizeBytes();
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
    case AssetType::TEXTURE:
        if (Texture *texture = asset.getUnderlyingAsset<Texture>()) {
            return texture->serialize(metadata.getSourcePath().string());
        }
        break;
    case AssetType::PREFAB:
        if (Prefab *prefab = asset.getUnderlyingAsset<Prefab>()) {
            return prefab->serialize();
        }
        break;
    case AssetType::MATERIAL:
        if (BaseMaterial *material = asset.getUnderlyingAsset<BaseMaterial>()) {
            return material->serialize();
        }
        break;
    case AssetType::MATERIAL_INSTANCE:
        if (MaterialInstance *instance = asset.getUnderlyingAsset<MaterialInstance>()) {
            return instance->serialize();
        }
        break;
    case AssetType::SCENE:
        if (SceneAsset *scene = asset.getUnderlyingAsset<SceneAsset>()) {
            return scene->serialize();
        }
        break;
    case AssetType::MODULE:
        if (ModuleClass *module = asset.getUnderlyingAsset<ModuleClass>()) {
            return module->toBlob();
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
    case AssetType::TEXTURE:
        if (auto texture = Texture::deserialize(payload)) {
            asset.setAssetVariant(std::move(texture));
            return true;
        }
        break;
    case AssetType::MESH:
        if (auto mesh = Mesh::deserialize(payload)) {
            asset.setAssetVariant(std::move(mesh));
            return true;
        }
        break;
    case AssetType::PREFAB:
        if (auto prefab = Prefab::deserialize(payload)) {
            asset.setAssetVariant(std::move(prefab));
            return true;
        }
        break;
    case AssetType::MATERIAL:
        if (auto material = BaseMaterial::deserialize(payload)) {
            asset.setAssetVariant(std::move(material));
            return true;
        }
        break;
    case AssetType::MATERIAL_INSTANCE:
        if (auto instance = MaterialInstance::deserialize(payload)) {
            asset.setAssetVariant(std::move(instance));
            return true;
        }
        break;
    case AssetType::SCENE:
        if (auto scene = SceneAsset::deserialize(payload)) {
            asset.setAssetVariant(std::move(scene));
            return true;
        }
        break;
    case AssetType::MODULE:
        if (auto module = ModuleClass::fromBlob(payload)) {
            asset.setAssetVariant(std::move(module));
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
    if (auto *meshData = std::get_if<MeshImportData>(&data)) {
        // The Mesh keeps only GPU buffers, so serialize the source data before it builds
        payload = meshData->params.serialize();
        type = AssetType::MESH;
        return std::make_unique<Mesh>(meshData->params);
    }
    if (auto *prefabData = std::get_if<PrefabImportData>(&data)) {
        payload = prefabData->prefab->serialize();
        type = AssetType::PREFAB;
        return std::move(prefabData->prefab);
    }
    if (auto *materialData = std::get_if<BaseMaterialImportData>(&data)) {
        payload = materialData->material->serialize();
        type = AssetType::MATERIAL;
        return std::move(materialData->material);
    }
    if (auto *instanceData = std::get_if<MaterialInstanceImportData>(&data)) {
        payload = instanceData->instance->serialize();
        type = AssetType::MATERIAL_INSTANCE;
        return std::move(instanceData->instance);
    }
    if (auto *sceneData = std::get_if<SceneImportData>(&data)) {
        payload = sceneData->scene->serialize();
        type = AssetType::SCENE;
        return std::move(sceneData->scene);
    }
    if (auto *moduleData = std::get_if<ModuleImportData>(&data)) {
        payload = moduleData->module->toBlob();
        type = AssetType::MODULE;
        return std::move(moduleData->module);
    }

    type = AssetType::NONE;
    return std::monostate{};
}

AssetManagerEditor::AssetManagerEditor(const Telemetry *telemetry) : AssetManagerBase(), m_telemetry(telemetry) {}

AssetManagerEditor::~AssetManagerEditor()
{
    m_shuttingDown = true;
    // The pending writes hold refs into the assets and their metadata, so they have to go first
    m_pendingWrites.clear();
    m_deferredFrees.clear();
    m_loadedAssets.clear();
    m_assetRegistry.clear();
    m_defaultAssetHandles.clear();
}

bool AssetManagerEditor::isAssetHandleValid(AssetHandle handle) const
{
    return m_assetRegistry.find(handle) != m_assetRegistry.end();
}

bool AssetManagerEditor::isAssetLoaded(AssetHandle handle) const
{
    return m_loadedAssets.find(handle) != m_loadedAssets.end() && m_loadedAssets.at(handle)->isValid();
}

Asset &AssetManagerEditor::getAsset(AssetHandle handle)
{

    if (!isAssetHandleValid(handle)) {
        RP_CORE_ERROR("Invalid asset handle {}, it is not registered", handle);
        return Asset::null;
    }

    if (isAssetLoaded(handle)) {
        return *m_loadedAssets.at(handle);
    } else {
        AssetMetadata &metadata = getAssetMetadata(handle);
        if (!metadata) {
            RP_CORE_ERROR("Invalid asset metadata, import asset first");
            return Asset::null;
        }
        std::unique_ptr<Asset> asset = loadFromMetadata(handle, metadata);
        if (!asset) {
            RP_CORE_ERROR("Failed to load asset '{}'", metadata.getName());
            return Asset::null;
        }

        auto [it, inserted] = m_loadedAssets.insert_or_assign(handle, std::move(asset));
        metadata.sizeHintBytes = s_assetSizeHint(*it->second);
        return *it->second;
    }
}

AssetMetadata &AssetManagerEditor::getAssetMetadata(AssetHandle handle)
{
    auto it = m_assetRegistry.find(handle);
    if (it != m_assetRegistry.end()) {
        return *it->second;
    }
    return AssetMetadata::null;
}

const AssetMetadata &AssetManagerEditor::getAssetMetadata(AssetHandle handle) const
{
    auto it = m_assetRegistry.find(handle);
    if (it != m_assetRegistry.end()) {
        return *it->second;
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

    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata->getSourcePath() == request.source && metadata->importConfig == request.config) {
            return getAsset(handle);
        }
    }

    auto metadata = std::make_unique<AssetMetadata>();
    metadata->storageType = AssetStorageType::DISK;
    metadata->provenance = AssetProvenance{.sourcePath = request.source};
    metadata->name = request.name.empty() ? request.source.stem().string() : request.name;
    metadata->assetType = determineAssetType(request.source.string());
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
    AssetType type = AssetType::NONE;
    AssetVariant value = s_buildImportData(request.data, payload, type);
    if (type == AssetType::NONE) {
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

    // recorded on the metadata so a module can be filtered by class while its payload is evicted
    if (ModuleClass *module = asset->getUnderlyingAsset<ModuleClass>()) {
        metadata->moduleClass = &module->type();
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

    auto existing = m_assetRegistry.find(handle);
    if (existing == m_assetRegistry.end()) {
        info->metadata->assetPath = path;
        m_assetRegistry.insert_or_assign(handle, std::move(info->metadata));
    } else {
        existing->second->assetPath = path;
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
    case AssetType::TEXTURE: {
        auto defaultTexture = Texture::createDefaultWhiteTexture();
        if (!defaultTexture) {
            RP_CORE_ERROR("Failed to create default white texture");
            return Asset::null;
        }

        Asset &asset =
            registerReservedAsset(RE_WHITE_TEXTURE, std::move(defaultTexture), "<default_white_texture>", AssetType::TEXTURE);
        if (asset) {
            m_defaultAssetHandles[assetType] = asset.getHandle();
        }
        return asset;
    }
    case AssetType::MATERIAL_INSTANCE: {
        auto baseMaterial = MaterialManager::getMaterial("Default Material");
        if (!baseMaterial) {
            RP_CORE_ERROR("Failed to get default material");
            return Asset::null;
        }

        auto defaultMaterial = std::make_unique<MaterialInstance>(baseMaterial, "Default");
        Asset &asset = registerReservedAsset(RE_DEFAULT_MATERIAL_INSTANCE, std::move(defaultMaterial), "<default_material>",
                                             AssetType::MATERIAL_INSTANCE);
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
        return AssetType::TEXTURE;
    } else if (extension == ".cubemap") {
        return AssetType::CUBEMAP;
    } else if (extension == ".gltf" || extension == ".glb" || extension == ".fbx") {
        return AssetType::PREFAB;
    } else if (extension == ".rscene") {
        return AssetType::SCENE;
    } else if (extension == ".rmat") {
        return AssetType::MATERIAL;
    } else if (extension == ".spv" || extension == ".glsl") {
        return AssetType::SHADER;
    }

    RP_CORE_WARN("Unknown asset type for extension: {}", extension);
    return AssetType::NONE;
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

    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata->isVirtualAsset() && metadata->name == virtualName && metadata->assetType == assetType) {
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

    auto [it, _] = m_loadedAssets.insert_or_assign(handle, std::move(asset));
    m_assetRegistry.insert_or_assign(handle, std::move(metadata));

    RP_CORE_INFO("Registered virtual {} asset: '{}'", AssetTypeToString(assetType), virtualName);
    return *it->second;
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

    if (auto it = m_loadedAssets.find(handle); it != m_loadedAssets.end()) {
        return *it->second;
    }

    auto asset = std::make_unique<Asset>(std::move(assetValue), handle);
    asset->status = AssetStatus::LOADED;

    auto metadata = std::make_unique<AssetMetadata>();
    metadata->assetType = assetType;
    metadata->storageType = AssetStorageType::VIRTUAL;
    metadata->name = name;
    metadata->sizeHintBytes = s_assetSizeHint(*asset);

    auto [it, _] = m_loadedAssets.insert_or_assign(handle, std::move(asset));
    m_assetRegistry.insert_or_assign(handle, std::move(metadata));
    return *it->second;
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
        if (metadata->assetType != AssetType::SHADER) {
            RP_CORE_ERROR("No output folder provided for '{}'", metadata->name);
        }
    } else if (!payload.empty()) {
        writeRaptureAssetFile(handle, outputFolder, *metadata, payload);
    } else {
        deferWrite = true;
    }

    auto loadedIt = m_loadedAssets.insert_or_assign(handle, std::move(asset)).first;
    auto registryIt = m_assetRegistry.insert_or_assign(handle, std::move(metadata)).first;

    if (deferWrite) {
        m_pendingWrites.push_back({AssetRef(loadedIt->second.get(), &registryIt->second->useCount), outputFolder});
    }

    return *loadedIt->second;
}

bool AssetManagerEditor::unregisterVirtualAsset(AssetHandle handle)
{
    auto registryIt = m_assetRegistry.find(handle);
    if (registryIt == m_assetRegistry.end()) {
        RP_CORE_WARN("Asset handle not found in registry");
        return false;
    }

    const AssetMetadata &metadata = *registryIt->second;
    if (!metadata.isVirtualAsset()) {
        RP_CORE_ERROR("Cannot unregister non-virtual asset: {}", metadata.name);
        return false;
    }

    m_loadedAssets.erase(handle);
    m_assetRegistry.erase(handle);

    RP_CORE_INFO("Unregistered virtual asset: '{}'", metadata.name);
    return true;
}

void AssetManagerEditor::registerBuiltinAssets()
{
    registerReservedAsset(RE_PRIMITIVE_CUBE_MESH, std::make_unique<Mesh>(Primitives::CreateCube()), "<cube>", AssetType::MESH);
    registerReservedAsset(RE_PRIMITIVE_SPHERE_MESH,
                          std::make_unique<Mesh>(Primitives::CreateSphere(PRIMITIVE_SPHERE_RADIUS, PRIMITIVE_SPHERE_SEGMENTS)),
                          "<sphere>", AssetType::MESH);
    registerReservedAsset(RE_PRIMITIVE_PLANE_MESH, std::make_unique<Mesh>(Primitives::CreatePlane()), "<plane>", AssetType::MESH);

    importDefaultAsset(AssetType::TEXTURE);
    importDefaultAsset(AssetType::MATERIAL_INSTANCE);
}

Asset &AssetManagerEditor::getVirtualAssetByName(const std::string &virtualName)
{
    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata->isVirtualAsset() && metadata->name == virtualName) {
            return getAsset(handle);
        }
    }
    return Asset::null;
}

std::vector<AssetHandle> AssetManagerEditor::getVirtualAssetsByType(AssetType type) const
{
    std::vector<AssetHandle> result;
    for (const auto &[handle, metadata] : m_assetRegistry) {
        if (metadata->isVirtualAsset() && metadata->assetType == type) {
            result.push_back(handle);
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
    auto metadataIt = m_assetRegistry.find(handle);
    if (metadataIt == m_assetRegistry.end()) {
        RP_CORE_ERROR("cannot update unregistered asset {}", handle);
        return false;
    }

    AssetMetadata &metadata = *metadataIt->second;
    if (metadata.assetPath.empty()) {
        RP_CORE_ERROR("'{}' has no file to update", metadata.getName());
        return false;
    }

    auto loadedIt = m_loadedAssets.find(handle);
    if (loadedIt == m_loadedAssets.end()) {
        auto loaded = std::make_unique<Asset>(handle);
        loadedIt = m_loadedAssets.emplace(handle, std::move(loaded)).first;
    }

    Asset &loadedAsset = *loadedIt->second;
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
        auto it = m_loadedAssets.find(handle);
        if (it != m_loadedAssets.end()) {
            AssetStatus status = it->second->getStatus();
            if (status == AssetStatus::REQUESTED || status == AssetStatus::LOADING) {
                stillLoading.push_back(handle);
                continue;
            }
        }

        const AssetMetadata &metadata = getAssetMetadata(handle);
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
    auto it = m_loadedAssets.find(handle);
    if (it == m_loadedAssets.end()) {
        return false;
    }

    // Engine builtins live under reserved handles and are never evicted
    if (Asset_isReserved(handle)) {
        return false;
    }

    const AssetMetadata &metadata = getAssetMetadata(handle);
    uint32_t refCount = metadata.useCount.load(std::memory_order_acquire);
    if (refCount != 0) {
        RP_CORE_WARN("Cannot unload asset({}) still in use, {} references remain", AssetTypeToString(metadata.assetType), refCount);
        return false;
    }

    RP_CORE_INFO("Evicting {} asset '{}'", AssetTypeToString(metadata.assetType), metadata.name);

    ensureDeferredFreeBuckets();
    m_deferredFrees[m_deferredFreeBucket].push_back(std::move(it->second));
    m_loadedAssets.erase(it);

    bool isVirtual = metadata.isVirtualAsset();
    if (isVirtual) {
        m_assetRegistry.erase(handle);
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
