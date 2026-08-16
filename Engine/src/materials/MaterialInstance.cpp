#include "MaterialInstance.h"

#include "asset_manager/Asset.h"
#include "asset_manager/AssetManager.h"
#include "graph/SurfaceGraphManager.h"
#include "logging/Log.h"
#include "textures/Texture.h"

namespace Rapture {

static constexpr uint32_t INSTANCE_BLOB_MAGIC = 0x494D5052; // "RPMI", identifies the blob as a material instance

// Version packs major in the high 16 bits and minor in the low 16, matching the other asset blobs.
static constexpr uint16_t INSTANCE_BLOB_VERSION_MAJOR = 1;
static constexpr uint16_t INSTANCE_BLOB_VERSION_MINOR = 0;
static constexpr uint32_t INSTANCE_BLOB_VERSION =
    (static_cast<uint32_t>(INSTANCE_BLOB_VERSION_MAJOR) << 16) | INSTANCE_BLOB_VERSION_MINOR;

struct InstanceBlobHeader {
    uint32_t magic = INSTANCE_BLOB_MAGIC;
    uint32_t version = INSTANCE_BLOB_VERSION;
    uint32_t sliceCount = 0;
    uint32_t textureCount = 0;
    uint32_t nameOffset = 0;
    uint32_t sliceOffset = 0;
    uint32_t texturesOffset = 0;
    uint32_t reserved = 0;
};

MaterialInstance::MaterialInstance(AssetPtr<BaseMaterial> material, const std::string &name)
    : m_baseMaterial(std::move(material)), m_bindlessIndex(UINT32_MAX)
{
    if (!m_baseMaterial) {
        RP_CORE_ERROR("MaterialInstance created from an asset that is not a base material");
        return;
    }

    m_name = name.empty() ? m_baseMaterial->getName() + "_instance" : name;
    m_bindlessIndex = MaterialManager::allocateSlot();

    uint32_t graphId = m_baseMaterial->getGraphId();
    SurfaceGraphManager &graphs = MaterialManager::getSurfaceGraphManager();
    m_slice = graphs.getDefaults(graphId);

    m_data = MaterialData::createDefault();
    setGraph(graphId, m_slice, graphs.getTextureRefs(graphId));
}

MaterialInstance::~MaterialInstance()
{
    if (m_bindlessIndex != UINT32_MAX) {
        MaterialManager::freeSlot(m_bindlessIndex);
    }
    MaterialManager::freeGraphData(m_graphData);
}

void MaterialInstance::setGraph(uint32_t graphId, const GraphInstanceData &data, std::vector<AssetPtr<Texture>> textures)
{
    // Retain the textures the slice indexes so they outlive eviction while this instance uses them
    m_graphTextureRefs = std::move(textures);

    // The slice size is graph specific, so a structural change reallocates from scratch
    MaterialManager::freeGraphData(m_graphData);

    uint32_t sizeBytes = static_cast<uint32_t>(data.size() * sizeof(uint32_t));
    if (sizeBytes > 0) {
        m_graphData = MaterialManager::allocateGraphData(sizeBytes);
        if (m_graphData.isValid()) {
            MaterialManager::writeGraphData(m_graphData, data);
        }
    }

    m_data.graphId = graphId;
    m_data.graphDataOffset = static_cast<uint32_t>(m_graphData.getOffsetBytes() / sizeof(uint32_t));
    syncToGPU();
    AssetEvents::onMaterialInstanceChanged().publish(this);
}

AssetPtr<Texture> MaterialInstance::getTextureRef(const ParameterId &id) const
{
    for (const auto &entry : m_textureRefs) {
        if (entry.first == id) {
            return entry.second;
        }
    }
    return {};
}

void MaterialInstance::setParameter(const ParameterId &id, AssetRef textureAsset)
{
    AssetPtr<Texture> texturePtr(textureAsset);
    Texture *texture = texturePtr.get();
    if (texture == nullptr) {
        return;
    }

    m_graphTextureRefs.push_back(texturePtr);
    m_textureRefs.emplace_back(id, texturePtr);

    if (texture->isReady()) {
        uint32_t bindlessIdx = texture->getBindlessIndex();
        writeSlice(id, &bindlessIdx, sizeof(uint32_t));
    } else {
        std::lock_guard<std::mutex> lock(m_pendingTexturesMutex);
        m_pendingTextures.push_back({id, texture});
    }
}

void MaterialInstance::updatePendingTextures()
{
    std::lock_guard<std::mutex> lock(m_pendingTexturesMutex);
    if (m_pendingTextures.empty()) return;

    m_pendingTextures.erase(std::remove_if(m_pendingTextures.begin(), m_pendingTextures.end(),
                                           [this](const PendingTexture &pending) {
                                               if (!pending.texture || !pending.texture->isReady()) {
                                                   return false;
                                               }

                                               uint32_t bindlessIdx = pending.texture->getBindlessIndex();
                                               writeSlice(pending.parameterId, &bindlessIdx, sizeof(uint32_t));
                                               return true;
                                           }),
                            m_pendingTextures.end());
}

void MaterialInstance::writeSlice(const ParameterId &id, const void *data, size_t size)
{
    uint32_t offset = 0;
    if (!m_baseMaterial->tryGetOffset(id, offset)) return;

    uint32_t words = static_cast<uint32_t>(size / sizeof(uint32_t));
    if (m_slice.size() < offset + words) m_slice.resize(offset + words, 0u);
    std::memcpy(&m_slice[offset], data, size);

    if (m_graphData.isValid()) {
        MaterialManager::writeGraphData(m_graphData, std::span<const uint32_t>(&m_slice[offset], words), offset);
    }
    AssetEvents::onMaterialInstanceChanged().publish(this);
}

void MaterialInstance::syncToGPU()
{
    MaterialManager::writeSlot(m_bindlessIndex, m_data);
}

std::vector<uint8_t> MaterialInstance::serialize() const
{
    auto appendBytes = [](std::vector<uint8_t> &out, const void *data, size_t size) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    auto appendU32 = [&](std::vector<uint8_t> &out, uint32_t v) { appendBytes(out, &v, sizeof(v)); };
    auto appendU64 = [&](std::vector<uint8_t> &out, uint64_t v) { appendBytes(out, &v, sizeof(v)); };
    auto appendString = [&](std::vector<uint8_t> &out, std::string_view s) {
        appendU32(out, static_cast<uint32_t>(s.size()));
        appendBytes(out, s.data(), s.size());
    };

    AssetHandle baseHandle = m_baseMaterial ? m_baseMaterial.ref().get()->getHandle() : INVALID_ASSET_HANDLE;

    std::vector<uint8_t> baseSection;
    appendU64(baseSection, baseHandle);

    std::vector<uint8_t> nameSection;
    appendString(nameSection, m_name);

    std::vector<uint8_t> sliceSection;
    for (uint32_t value : m_slice) {
        appendU32(sliceSection, value);
    }

    std::vector<uint8_t> textureSection;
    for (const auto &[param, texture] : m_textureRefs) {
        appendString(textureSection, param);
        appendU64(textureSection, texture ? texture.ref().get()->getHandle() : INVALID_ASSET_HANDLE);
    }

    InstanceBlobHeader header;
    header.sliceCount = static_cast<uint32_t>(m_slice.size());
    header.textureCount = static_cast<uint32_t>(m_textureRefs.size());
    header.nameOffset = static_cast<uint32_t>(sizeof(InstanceBlobHeader) + baseSection.size());
    header.sliceOffset = static_cast<uint32_t>(header.nameOffset + nameSection.size());
    header.texturesOffset = static_cast<uint32_t>(header.sliceOffset + sliceSection.size());

    std::vector<uint8_t> blob(sizeof(InstanceBlobHeader));
    std::memcpy(blob.data(), &header, sizeof(InstanceBlobHeader));
    blob.insert(blob.end(), baseSection.begin(), baseSection.end());
    blob.insert(blob.end(), nameSection.begin(), nameSection.end());
    blob.insert(blob.end(), sliceSection.begin(), sliceSection.end());
    blob.insert(blob.end(), textureSection.begin(), textureSection.end());
    return blob;
}

std::unique_ptr<MaterialInstance> MaterialInstance::deserialize(std::span<const uint8_t> blob)
{
    if (blob.size() < sizeof(InstanceBlobHeader)) {
        RP_CORE_ERROR("Material instance blob is smaller than its header");
        return nullptr;
    }

    InstanceBlobHeader header;
    std::memcpy(&header, blob.data(), sizeof(InstanceBlobHeader));
    if (header.magic != INSTANCE_BLOB_MAGIC) {
        RP_CORE_ERROR("Material instance blob has an invalid magic");
        return nullptr;
    }
    if ((header.version >> 16) != INSTANCE_BLOB_VERSION_MAJOR) {
        RP_CORE_ERROR("Material instance blob major version {} is unsupported", header.version >> 16);
        return nullptr;
    }

    size_t offset = 0;
    auto read = [&](void *dst, size_t size) -> bool {
        if (offset + size > blob.size()) {
            return false;
        }
        std::memcpy(dst, blob.data() + offset, size);
        offset += size;
        return true;
    };
    auto readU32 = [&](uint32_t &v) { return read(&v, sizeof(v)); };
    auto readU64 = [&](uint64_t &v) { return read(&v, sizeof(v)); };
    auto readString = [&](std::string &s) -> bool {
        uint32_t length = 0;
        if (!readU32(length) || offset + length > blob.size()) {
            return false;
        }
        s.assign(reinterpret_cast<const char *>(blob.data() + offset), length);
        offset += length;
        return true;
    };

    offset = sizeof(InstanceBlobHeader);
    AssetHandle baseHandle = INVALID_ASSET_HANDLE;
    if (!readU64(baseHandle)) {
        return nullptr;
    }

    AssetPtr<BaseMaterial> base(AssetManager::getAsset(baseHandle));
    if (!base) {
        RP_CORE_ERROR("Material instance references a missing base material");
        return nullptr;
    }

    offset = header.nameOffset;
    std::string name;
    if (!readString(name)) {
        return nullptr;
    }

    offset = header.sliceOffset;
    GraphInstanceData slice;
    slice.reserve(header.sliceCount);
    for (uint32_t i = 0; i < header.sliceCount; ++i) {
        uint32_t value = 0;
        if (!readU32(value)) {
            return nullptr;
        }
        slice.push_back(value);
    }

    offset = header.texturesOffset;
    std::vector<std::pair<ParameterId, AssetHandle>> textureDeps;
    textureDeps.reserve(header.textureCount);
    for (uint32_t i = 0; i < header.textureCount; ++i) {
        std::string param;
        AssetHandle textureHandle = INVALID_ASSET_HANDLE;
        if (!readString(param) || !readU64(textureHandle)) {
            return nullptr;
        }
        textureDeps.emplace_back(std::move(param), textureHandle);
    }

    auto instance = std::make_unique<MaterialInstance>(base, name);

    // Restore the scalar overrides before the texture slots are re-resolved to fresh bindless indices
    if (slice.size() == instance->m_slice.size() && instance->m_graphData.isValid()) {
        instance->m_slice = std::move(slice);
        MaterialManager::writeGraphData(instance->m_graphData, instance->m_slice);
    }

    for (const auto &[param, textureHandle] : textureDeps) {
        if (textureHandle == INVALID_ASSET_HANDLE) {
            continue;
        }

        AssetRef textureAsset = AssetManager::getAsset(textureHandle);
        if (!textureAsset) {
            RP_CORE_ERROR("Material instance '{}' references a missing texture {} for parameter '{}'", name, textureHandle, param);
            continue;
        }
        instance->setParameter(param, std::move(textureAsset));
    }

    return instance;
}

} // namespace Rapture
