#ifndef RAPTURE__MATERIAL_INSTANCE_H
#define RAPTURE__MATERIAL_INSTANCE_H

#include "Buffers/UniformBuffers/UniformBuffer.h"
#include "Events/AssetEvents.h"
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
        const ParamInfo *info = getParamInfo(id);
        if (!info) return;
        if (sizeof(T) != info->size) return;

        char *dataPtr = reinterpret_cast<char *>(&m_data);
        std::memcpy(dataPtr + info->offset, &value, info->size);
        syncToGPU();
        AssetEvents::onMaterialInstanceChanged().publish(this);
    }

    template <typename T> T getParameter(ParameterID id) const
    {
        const ParamInfo *info = getParamInfo(id);
        if (!info || sizeof(T) != info->size) return T{};

        T value{};
        const char *dataPtr = reinterpret_cast<const char *>(&m_data);
        std::memcpy(&value, dataPtr + info->offset, info->size);
        return value;
    }

    void setParameter(ParameterID id, Texture *texture);
    void updatePendingTextures();

  private:
    void syncToGPU();

    std::string m_name;
    std::shared_ptr<BaseMaterial> m_baseMaterial;
    std::shared_ptr<UniformBuffer> m_uniformBuffer;
    uint32_t m_bindlessIndex;

    MaterialData m_data;

    std::vector<PendingTexture> m_pendingTextures;
    std::mutex m_pendingTexturesMutex;
};

} // namespace Rapture

#endif // RAPTURE__MATERIAL_INSTANCE_H
