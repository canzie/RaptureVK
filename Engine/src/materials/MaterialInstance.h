#ifndef RAPTURE__MATERIAL_INSTANCE_H
#define RAPTURE__MATERIAL_INSTANCE_H

#include "asset_manager/AssetHandle.h"
#include "events/AssetEvents.h"
#include "GraphInstanceData.h"
#include "Material.h"
#include "MaterialData.h"
#include "MaterialParameters.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Rapture {

class Texture;

struct PendingTexture {
    ParameterID parameterId;
    Texture *texture;
};

class MaterialInstance {
  public:
    MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string &name = "");
    ~MaterialInstance();

    std::shared_ptr<BaseMaterial> getBaseMaterial() const { return m_baseMaterial; }
    const std::string &getName() const { return m_name; }
    uint32_t getBindlessIndex() const { return m_bindlessIndex; }
    const MaterialData &getData() const { return m_data; }
    uint32_t getFlags() const { return m_data.flags; }

    template <typename T> void setParameter(ParameterID id, const T &value)
    {
        writeSlice(id, &value, sizeof(T));
    }

    template <typename T> T getParameter(ParameterID id) const
    {
        T value{};
        uint32_t offset = 0;
        if (!m_baseMaterial->tryGetOffset(id, offset)) return value;
        if (offset + sizeof(T) / sizeof(uint32_t) <= m_slice.size()) {
            std::memcpy(&value, &m_slice[offset], sizeof(T));
        }
        return value;
    }

    void setParameter(ParameterID id, AssetRef texture);
    void updatePendingTextures();

    /**
     * @brief The texture bound to a texture parameter, for reading a material back into a graph
     * @param id The texture parameter to look up
     * @return The bound texture, or null if none is set
     */
    AssetPtr<Texture> getTextureRef(ParameterID id) const;

    /**
     * @brief Turn this instance into a graph material backed by a generated surface function
     * @param graphId Which generated evalSurface_* to dispatch to
     * @param data Graph instance pool (compiler-assigned textures and values)
     * @param textures Textures the pool indexes, retained so they are not evicted while in use
     */
    void setGraph(uint32_t graphId, const GraphInstanceData &data, std::vector<AssetPtr<Texture>> textures);

  private:
    void syncToGPU();
    void writeSlice(ParameterID id, const void *data, size_t size);

    std::string m_name;
    std::shared_ptr<BaseMaterial> m_baseMaterial;
    uint32_t m_bindlessIndex;
    uint32_t m_graphDataOffset = UINT32_MAX;

    MaterialData m_data;
    GraphInstanceData m_slice;

    std::vector<PendingTexture> m_pendingTextures;
    std::mutex m_pendingTexturesMutex;

    std::vector<std::pair<ParameterID, AssetPtr<Texture>>> m_textureRefs;
    std::vector<AssetPtr<Texture>> m_graphTextureRefs;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_INSTANCE_H
