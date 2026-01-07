#include "MaterialInstance.h"

#include "Buffers/Descriptors/DescriptorManager.h"
#include "Logging/Log.h"
#include "Textures/Texture.h"
#include "WindowContext/Application.h"

namespace Rapture {

MaterialInstance::MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string &name)
    : m_baseMaterial(material), m_bindlessIndex(UINT32_MAX)
{
    m_name = name.empty() ? material->getName() + "_instance" : name;
    m_data = material->getDefaults();

    auto &app = Application::getInstance();
    auto allocator = app.getVulkanContext().getVmaAllocator();

    m_uniformBuffer = std::make_shared<UniformBuffer>(sizeof(MaterialData), BufferUsage::DYNAMIC, allocator, nullptr);

    auto materialSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::MATERIAL_UBO);
    if (materialSet) {
        auto binding = materialSet->getUniformBufferBinding(DescriptorSetBindingLocation::MATERIAL_UBO);
        if (binding) {
            m_bindlessIndex = binding->add(*m_uniformBuffer);
        }
    }

    syncToGPU();
}

MaterialInstance::~MaterialInstance()
{
    if (m_bindlessIndex != UINT32_MAX) {
        auto materialSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::MATERIAL_UBO);
        if (materialSet) {
            auto binding = materialSet->getUniformBufferBinding(DescriptorSetBindingLocation::MATERIAL_UBO);
            if (binding) {
                binding->free(m_bindlessIndex);
            }
        }
    }
}

void MaterialInstance::setParameter(ParameterID id, Texture *texture)
{
    const ParamInfo *info = getParamInfo(id);
    if (!info || info->type != ParamType::TEXTURE) return;

    if (texture && texture->isReady()) {
        uint32_t bindlessIdx = texture->getBindlessIndex();
        char *dataPtr = reinterpret_cast<char *>(&m_data);
        std::memcpy(dataPtr + info->offset, &bindlessIdx, sizeof(uint32_t));

        if (info->flag) {
            m_data.flags |= info->flag;
        }
        syncToGPU();
        AssetEvents::onMaterialInstanceChanged().publish(this);
    } else if (texture) {
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
                                               syncToGPU();
                                               AssetEvents::onMaterialInstanceChanged().publish(this);
                                               return true;
                                           }),
                            m_pendingTextures.end());
}

void MaterialInstance::syncToGPU()
{
    m_uniformBuffer->addData(&m_data, sizeof(MaterialData), 0);
}

} // namespace Rapture
