#ifndef RAPTURE__MATERIAL_INSTANCE_H
#define RAPTURE__MATERIAL_INSTANCE_H

#include "GraphInstanceData.h"
#include "Material.h"
#include "MaterialData.h"
#include "MaterialParameters.h"
#include "assets/asset_manager/Asset.h"
#include "assets/materials/AMaterial.h"
#include "assets/textures/ATexture.h"
#include "core/events/AssetEvents.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace Rapture {

class Texture;

struct PendingTexture {
    ParameterId parameterId;
    Texture *texture;
};

class MaterialInstance {
  public:
    MaterialInstance(Ref<AMaterial> material, const std::string &name = "");
    ~MaterialInstance();

    BaseMaterial *getBaseMaterial() const { return m_baseMaterial ? &m_baseMaterial.get()->material() : nullptr; }

    std::vector<uint8_t> serialize() const;
    static std::unique_ptr<MaterialInstance> deserialize(std::span<const uint8_t> blob);

    const std::string &getName() const { return m_name; }
    uint32_t getBindlessIndex() const { return m_bindlessIndex; }
    const MaterialData &getData() const { return m_data; }
    uint32_t getFlags() const { return m_data.flags; }

    template <typename T>
    void setParameter(const ParameterId &id, const T &value)
    {
        writeSlice(id, &value, sizeof(T));
    }

    template <typename T>
    T getParameter(const ParameterId &id) const
    {
        T value{};
        uint32_t offset = 0;
        if (!m_baseMaterial->tryGetOffset(id, offset)) {
            return value;
        }
        if (offset + sizeof(T) / sizeof(uint32_t) <= m_slice.size()) {
            std::memcpy(&value, &m_slice[offset], sizeof(T));
        }
        return value;
    }

    void setParameter(const ParameterId &id, Ref<ATexture> texture);
    void updatePendingTextures();

    /**
     * @brief The texture bound to a texture parameter, for reading a material back into a graph
     * @param id The texture parameter to look up
     * @return The bound texture, or null if none is set
     */
    Ref<ATexture> getTextureRef(const ParameterId &id) const;

    /**
     * @brief Turn this instance into a graph material backed by a generated surface function
     * @param graphId Which generated evalSurface_* to dispatch to
     * @param data Graph instance pool (compiler-assigned textures and values)
     * @param textures Textures the pool indexes, retained so they are not evicted while in use
     */
    void setGraph(uint32_t graphId, const GraphInstanceData &data, std::vector<Ref<ATexture>> textures);

  private:
    void syncToGPU();
    void writeSlice(const ParameterId &id, const void *data, size_t size);

    std::string m_name;
    Ref<AMaterial> m_baseMaterial;
    uint32_t m_bindlessIndex;
    VirtualStorageBuffer::Allocation m_graphData;

    MaterialData m_data;
    GraphInstanceData m_slice;

    std::vector<PendingTexture> m_pendingTextures;
    std::mutex m_pendingTexturesMutex;

    std::vector<std::pair<ParameterId, Ref<ATexture>>> m_textureRefs;
    std::vector<Ref<ATexture>> m_graphTextureRefs;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_INSTANCE_H
