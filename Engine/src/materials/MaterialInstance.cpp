#include "MaterialInstance.h"

#include "asset_manager/Asset.h"
#include "logging/Log.h"
#include "textures/Texture.h"

namespace Rapture {

MaterialInstance::MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string &name)
    : m_baseMaterial(material), m_bindlessIndex(UINT32_MAX)
{
    m_name = name.empty() ? material->getName() + "_instance" : name;
    m_data = material->getDefaults();

    m_bindlessIndex = MaterialManager::allocateSlot();

    syncToGPU();
}

MaterialInstance::~MaterialInstance()
{
    if (m_bindlessIndex != UINT32_MAX) {
        MaterialManager::freeSlot(m_bindlessIndex);
    }
    if (m_graphDataOffset != UINT32_MAX) {
        MaterialManager::freeGraphData(m_graphDataOffset);
    }
}

void MaterialInstance::setGraph(uint32_t graphId, const GraphInstanceData &data, std::vector<AssetPtr<Texture>> textures)
{
    // Retain the textures the slice indexes so they outlive eviction while this instance uses them
    m_graphTextureRefs = std::move(textures);

    // The slice size is graph specific, so a structural change reallocates from scratch
    if (m_graphDataOffset != UINT32_MAX) {
        MaterialManager::freeGraphData(m_graphDataOffset);
        m_graphDataOffset = UINT32_MAX;
    }

    uint32_t sizeBytes = static_cast<uint32_t>(data.size() * sizeof(uint32_t));
    if (sizeBytes > 0) {
        m_graphDataOffset = MaterialManager::allocateGraphData(sizeBytes);
        if (m_graphDataOffset != UINT32_MAX) {
            MaterialManager::writeGraphData(m_graphDataOffset, data.data(), sizeBytes);
        }
    }

    m_data.flags |= MAT_FLAG_IS_GRAPH;
    m_data.graphId = graphId;
    m_data.graphDataOffset = m_graphDataOffset == UINT32_MAX ? 0u : m_graphDataOffset;
    syncToGPU();
    AssetEvents::onMaterialInstanceChanged().publish(this);
}

void MaterialInstance::setParameter(ParameterID id, AssetRef textureAsset)
{
    const ParamInfo *info = getParamInfo(id);
    if (!info || info->type != ParamType::TEXTURE) return;

    AssetPtr<Texture> texturePtr(textureAsset);
    Texture *texture = texturePtr.get();

    auto it = std::find_if(m_textureRefs.begin(), m_textureRefs.end(),
                           [id](const std::pair<ParameterID, AssetPtr<Texture>> &entry) { return entry.first == id; });
    if (texture != nullptr) {
        if (it != m_textureRefs.end()) {
            it->second = std::move(texturePtr);
        } else {
            m_textureRefs.emplace_back(id, std::move(texturePtr));
        }
    } else if (it != m_textureRefs.end()) {
        m_textureRefs.erase(it);
    }

    if (texture != nullptr && texture->isReady()) {
        uint32_t bindlessIdx = texture->getBindlessIndex();
        char *dataPtr = reinterpret_cast<char *>(&m_data);
        std::memcpy(dataPtr + info->offset, &bindlessIdx, sizeof(uint32_t));

        if (info->flag) {
            m_data.flags |= info->flag;
        }
        applyTextureEncodingFlags(id, texture);
        syncToGPU();
        AssetEvents::onMaterialInstanceChanged().publish(this);
    } else if (texture != nullptr) {
        std::lock_guard<std::mutex> lock(m_pendingTexturesMutex);
        m_pendingTextures.push_back({id, texture});
    } else if (info->flag && (m_data.flags & info->flag)) {
        m_data.flags &= ~info->flag;
        syncToGPU();
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

                                               const ParamInfo *info = getParamInfo(pending.parameterId);
                                               if (!info) return true;

                                               uint32_t bindlessIdx = pending.texture->getBindlessIndex();
                                               char *dataPtr = reinterpret_cast<char *>(&m_data);
                                               std::memcpy(dataPtr + info->offset, &bindlessIdx, sizeof(uint32_t));

                                               if (info->flag) {
                                                   m_data.flags |= info->flag;
                                               }
                                               applyTextureEncodingFlags(pending.parameterId, pending.texture);
                                               syncToGPU();
                                               AssetEvents::onMaterialInstanceChanged().publish(this);
                                               return true;
                                           }),
                            m_pendingTextures.end());
}

void MaterialInstance::applyTextureEncodingFlags(ParameterID id, Texture *texture)
{
    if (id != ParameterID::NORMAL_MAP) {
        return;
    }

    if (texture->getSpecification().format == TextureFormat::BC5) {
        m_data.flags |= MAT_FLAG_NORMAL_BC5;
    } else {
        m_data.flags &= ~MAT_FLAG_NORMAL_BC5;
    }
}

void MaterialInstance::syncToGPU()
{
    MaterialManager::writeSlot(m_bindlessIndex, m_data);
}

} // namespace Rapture
