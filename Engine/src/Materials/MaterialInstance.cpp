#include "MaterialInstance.h"

#include "AssetManager/AssetManager.h"
#include "Materials/MaterialParameters.h"
#include "Renderer/DeferredShading/GBufferPass.h"
#include "Textures/Texture.h"
#include "WindowContext/Application.h"

namespace Rapture {

MaterialInstance::MaterialInstance(std::shared_ptr<BaseMaterial> material, const std::string &name)
    : m_bindlessUniformBufferIndex(UINT32_MAX), m_baseMaterial(material)
{

    auto &app = Application::getInstance();
    auto allocator = app.getVulkanContext().getVmaAllocator();

    if (name.empty()) {
        m_name = material->getName() + "_instance";
    } else {
        m_name = name;
    }

    if (allocator == nullptr) {
        RP_CORE_ERROR("allocator is nullptr!");
        throw std::runtime_error("allocator is nullptr!");
    }

    // Create uniform buffer
    m_uniformBuffer = std::make_shared<UniformBuffer>(material->getSizeBytes(), BufferUsage::DYNAMIC, allocator, nullptr);
    auto materialSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::MATERIAL_UBO);
    if (materialSet) {
        auto binding = materialSet->getUniformBufferBinding(DescriptorSetBindingLocation::MATERIAL_UBO);
        if (binding) {
            m_bindlessUniformBufferIndex = binding->add(*m_uniformBuffer);
        }
    }

    m_parameterMap = material->getTemplateParameters();

    // Get the default white texture for fallback
    auto [defaultTextureAsset, defaultTextureHandle] = AssetManager::importDefaultAsset<Texture>(AssetType::Texture);
    uint32_t defaultWhiteTextureIndex = 0; // fallback to index 0 if no default found
    (void)defaultWhiteTextureIndex;

    if (defaultTextureAsset) {
        if (defaultTextureAsset->isReadyForSampling()) {
            defaultWhiteTextureIndex = defaultTextureAsset->getBindlessIndex();
        }
    }

    for (auto &[id, param] : m_parameterMap) {
        // Initialize texture parameters with default white texture bindless index
        if (param.m_info.type == MaterialParameterTypes::UINT) {
            // Check if this parameter is a texture type
            if (isTextureParameter(id)) {
                param.setValue<uint32_t>(UINT32_MAX);
            }
        }

        m_uniformBuffer->addData(param.asRaw(), param.m_info.size, param.m_info.offset);
    }
}

MaterialInstance::~MaterialInstance()
{

    if (m_bindlessUniformBufferIndex != UINT32_MAX) {
        auto materialSet = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::MATERIAL_UBO);
        if (materialSet) {
            auto binding = materialSet->getUniformBufferBinding(DescriptorSetBindingLocation::MATERIAL_UBO);
            if (binding) {
                binding->free(m_bindlessUniformBufferIndex);
            }
        }
    }
}

MaterialParameter MaterialInstance::getParameter(ParameterID id)
{

    if (m_parameterMap.find(id) != m_parameterMap.end()) return m_parameterMap[id];

    return MaterialParameter();
}

void MaterialInstance::setParameter(ParameterID id, std::shared_ptr<Texture> texture)
{

    if (m_parameterMap.find(id) != m_parameterMap.end()) {
        if (texture && texture->isReadyForSampling()) {
            m_parameterMap[id].setValue<uint32_t>(texture->getBindlessIndex());
            updateUniformBuffer(id);
            m_flagsDirty = true;

            Rapture::AssetEvents::onMaterialInstanceChanged().publish(this);
        } else {
            std::lock_guard<std::mutex> lock(m_pendingTexturesMutex);
            m_pendingTextures.push_back({id, texture});
        }
    } else {
        RP_CORE_WARN("Parameter ID '{}' not found for this material", parameterIdToString(id));
    }
}

void MaterialInstance::updatePendingTextures()
{
    std::lock_guard<std::mutex> lock(m_pendingTexturesMutex);

    if (m_pendingTextures.empty()) {
        return;
    }

    m_pendingTextures.erase(std::remove_if(m_pendingTextures.begin(), m_pendingTextures.end(),
                                           [this](const PendingTexture &pending) {
                                               bool isReadyForSampling = pending.texture && pending.texture->isReadyForSampling();
                                               bool isNullTexture = !pending.texture;

                                               if (isReadyForSampling || isNullTexture) {
                                                   uint32_t bindlessIndex =
                                                       isNullTexture ? UINT32_MAX : pending.texture->getBindlessIndex();

                                                   m_parameterMap[pending.parameterId].setValue<uint32_t>(bindlessIndex);
                                                   updateUniformBuffer(pending.parameterId);
                                                   m_flagsDirty = true;

                                                   Rapture::AssetEvents::onMaterialInstanceChanged().publish(this);
                                                   // Return true to remove it from the pending list.
                                                   return true;
                                               }

                                               return false;
                                           }),
                            m_pendingTextures.end());
}

void MaterialInstance::updateUniformBuffer(ParameterID id)
{
    m_uniformBuffer->addData(m_parameterMap[id].asRaw(), m_parameterMap[id].m_info.size, m_parameterMap[id].m_info.offset);
}

uint32_t MaterialInstance::getMaterialFlags() const
{
    if (m_flagsDirty) {
        m_materialFlags = calculateMaterialFlags();
        m_flagsDirty = false;
    }
    return m_materialFlags;
}

void MaterialInstance::recalculateMaterialFlags()
{
    m_flagsDirty = true;
    getMaterialFlags(); // Force recalculation
}

uint32_t MaterialInstance::calculateMaterialFlags() const
{
    uint32_t flags = 0;

    // Check each texture parameter and set corresponding flag using GBufferFlags enum
    if (hasValidTexture(ParameterID::ALBEDO_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_ALBEDO_MAP);
    }

    if (hasValidTexture(ParameterID::NORMAL_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_NORMAL_MAP);
    }

    if (hasValidTexture(ParameterID::METALLIC_ROUGHNESS_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_METALLIC_ROUGHNESS_MAP);
    }

    if (hasValidTexture(ParameterID::AO_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_AO_MAP);
    }

    if (hasValidTexture(ParameterID::METALLIC_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_METALLIC_MAP);
    }

    if (hasValidTexture(ParameterID::ROUGHNESS_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_ROUGHNESS_MAP);
    }

    if (hasValidTexture(ParameterID::EMISSIVE_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_EMISSIVE_MAP);
    }

    if (hasValidTexture(ParameterID::SPECULAR_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_SPECULAR_MAP);
    }

    if (hasValidTexture(ParameterID::HEIGHT_MAP)) {
        flags |= static_cast<uint32_t>(GBufferFlags::HAS_HEIGHT_MAP);
    }

    return flags;
}

bool MaterialInstance::hasValidTexture(ParameterID id) const
{
    auto it = m_parameterMap.find(id);
    if (it == m_parameterMap.end()) {
        return false;
    }

    auto param = it->second;

    // Check if it's a texture parameter
    if (param.m_info.type == MaterialParameterTypes::UINT) {
        uint32_t value = param.asUInt();
        return value != UINT32_MAX && value != 0;
    }

    // Check if it holds a valid (non-null) texture
    if (std::holds_alternative<std::shared_ptr<Texture>>(param.m_value)) {
        auto texture = std::get<std::shared_ptr<Texture>>(param.m_value);
        return texture != nullptr;
    }

    return false;
}

// Helper function to check if a parameter ID represents a texture
bool MaterialInstance::isTextureParameter(ParameterID id) const
{
    switch (id) {
    case ParameterID::ALBEDO_MAP:
    case ParameterID::NORMAL_MAP:
    case ParameterID::METALLIC_MAP:
    case ParameterID::ROUGHNESS_MAP:
    case ParameterID::METALLIC_ROUGHNESS_MAP:
    case ParameterID::HEIGHT_MAP:
    case ParameterID::AO_MAP:
    case ParameterID::EMISSIVE_MAP:
    case ParameterID::SPECULAR_MAP:
        return true;
    default:
        return false;
    }
}

} // namespace Rapture
