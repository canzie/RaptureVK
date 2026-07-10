#include "MaterialInstance.h"

#include "asset_manager/Asset.h"
#include "graph/SurfaceGraphManager.h"
#include "logging/Log.h"
#include "textures/Texture.h"

namespace Rapture {

MaterialInstance::MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string &name)
    : m_baseMaterial(material), m_bindlessIndex(UINT32_MAX)
{
    m_name = name.empty() ? material->getName() + "_instance" : name;
    m_bindlessIndex = MaterialManager::allocateSlot();

    uint32_t graphId = material->getGraphId();
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

AssetPtr<Texture> MaterialInstance::getTextureRef(ParameterID id) const
{
    for (const auto &entry : m_textureRefs) {
        if (entry.first == id) {
            return entry.second;
        }
    }
    return {};
}

void MaterialInstance::setParameter(ParameterID id, AssetRef textureAsset)
{
    AssetPtr<Texture> texturePtr(textureAsset);
    Texture *texture = texturePtr.get();
    if (texture == nullptr) return;

    m_graphTextureRefs.push_back(texturePtr);

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

void MaterialInstance::writeSlice(ParameterID id, const void *data, size_t size)
{
    uint32_t offset = 0;
    if (!m_baseMaterial->tryGetOffset(id, offset)) return;

    uint32_t words = static_cast<uint32_t>(size / sizeof(uint32_t));
    if (m_slice.size() < offset + words) m_slice.resize(offset + words, 0u);
    std::memcpy(&m_slice[offset], data, size);

    if (m_graphDataOffset != UINT32_MAX) {
        MaterialManager::writeGraphData(m_graphDataOffset + offset, data, static_cast<uint32_t>(size));
    }
    AssetEvents::onMaterialInstanceChanged().publish(this);
}

void MaterialInstance::syncToGPU()
{
    MaterialManager::writeSlot(m_bindlessIndex, m_data);
}

} // namespace Rapture
