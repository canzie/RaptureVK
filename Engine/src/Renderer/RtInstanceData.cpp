#include "RtInstanceData.h"

#include "AccelerationStructures/TLAS.h"
#include "Buffers/Descriptors/DescriptorManager.h"
#include "Buffers/Descriptors/DescriptorSet.h"
#include "Buffers/VertexBuffers/BufferLayout.h"
#include "Components/Components.h"
#include "Events/AssetEvents.h"
#include "Logging/Log.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialParameters.h"
#include "Meshes/Mesh.h"
#include "Scenes/Entities/Entity.h"
#include "WindowContext/Application.h"

namespace Rapture {

RtInstanceData::RtInstanceData()
{
    auto &app = Application::getInstance();
    auto &vc = app.getVulkanContext();
    m_allocator = vc.getVmaAllocator();

    AssetEvents::onMaterialInstanceChanged().addListener([this](MaterialInstance *mat) {
        if (mat) m_dirtyMaterials.insert(mat);
    });

    AssetEvents::onMeshTransformChanged().addListener([this](EntityID ent) {
        (void)ent;
        if (ent) m_dirtyTransforms.insert(ent);
    });
}

RtInstanceData::~RtInstanceData() {}

void RtInstanceData::markMaterialDirty(MaterialInstance *material)
{
    if (material) m_dirtyMaterials.insert(material);
}

void RtInstanceData::markTransformDirty(uint32_t entityID)
{
    m_dirtyTransforms.insert(entityID);
}

void RtInstanceData::update(std::shared_ptr<Scene> scene)
{
    auto tlas = scene->getTLAS();
    if (!tlas || !tlas->isBuilt() || tlas->getInstanceCount() == 0) {
        RP_CORE_ERROR("No TLAS found");
        return;
    }

    if (m_instanceCount != tlas->getInstanceCount() || m_lastTlasInstanceCount != tlas->getInstanceCount()) {
        rebuild(scene);
    } else {
        patchDirty(scene);
    }
}

void RtInstanceData::rebuild(std::shared_ptr<Scene> scene)
{
    auto tlas = scene->getTLAS();
    auto &tlasInstances = tlas->getInstances();
    auto &reg = scene->getRegistry();
    auto view = reg.view<MaterialComponent, MeshComponent, TransformComponent>(entt::exclude<LightComponent>);

    std::vector<RtInstanceInfo> infos(tlas->getInstanceCount());

    for (uint32_t i = 0; i < tlasInstances.size(); ++i) {
        auto &inst = tlasInstances[i];
        Entity ent = Entity(inst.entityID, scene.get());

        RtInstanceInfo &info = infos[i];
        info = {};
        info.AlbedoTextureIndex = UINT32_MAX;
        info.NormalTextureIndex = UINT32_MAX;
        info.EmissiveFactorTextureIndex = UINT32_MAX;
        info.albedo = glm::vec3(1.0f);
        info.emissiveColor = glm::vec3(0.0f);

        if (view.contains(ent)) {
            auto [meshComp, materialComp, transformComp] = view.get<MeshComponent, MaterialComponent, TransformComponent>(ent);

            info.modelMatrix = transformComp.transformMatrix();

            if (materialComp.material) {
                auto &mat = materialComp.material;
                info.AlbedoTextureIndex = mat->getParameter(ParameterID::ALBEDO_MAP).asUInt();
                info.NormalTextureIndex = mat->getParameter(ParameterID::NORMAL_MAP).asUInt();
                info.EmissiveFactorTextureIndex = mat->getParameter(ParameterID::EMISSIVE_MAP).asUInt();
                info.albedo = mat->getParameter(ParameterID::ALBEDO).asVec3();
                info.emissiveColor = mat->getParameter(ParameterID::EMISSIVE).asVec3();
            }

            if (meshComp.mesh) {
                auto vb = meshComp.mesh->getVertexBuffer();
                auto ib = meshComp.mesh->getIndexBuffer();

                if (vb) {
                    info.vboIndex = vb->getBindlessIndex();
                    auto &layout = vb->getBufferLayout();
                    info.positionAttributeOffsetBytes = layout.getAttributeOffset(BufferAttributeID::POSITION);
                    info.texCoordAttributeOffsetBytes = layout.getAttributeOffset(BufferAttributeID::TEXCOORD_0);
                    info.normalAttributeOffsetBytes = layout.getAttributeOffset(BufferAttributeID::NORMAL);
                    info.tangentAttributeOffsetBytes = layout.getAttributeOffset(BufferAttributeID::TANGENT);
                    info.vertexStrideBytes = layout.calculateVertexSize();
                }

                if (ib) {
                    info.iboIndex = ib->getBindlessIndex();
                    info.indexType = ib->getIndexType();
                }
            }
        }

        info.meshIndex = inst.instanceCustomIndex;
    }

    m_instanceCount = static_cast<uint32_t>(infos.size());
    m_lastTlasInstanceCount = tlas->getInstanceCount();

    if (!m_buffer || m_buffer->getSize() < sizeof(RtInstanceInfo) * infos.size()) {
        m_buffer =
            std::make_shared<StorageBuffer>(sizeof(RtInstanceInfo) * infos.size(), BufferUsage::DYNAMIC, m_allocator, infos.data());
    } else {
        m_buffer->addData(infos.data(), sizeof(RtInstanceInfo) * infos.size(), 0);
    }

    m_materialToOffsets.clear();
    m_entityToOffset.clear();

    for (uint32_t idx = 0; idx < infos.size(); ++idx) {
        Entity ent = Entity(tlasInstances[idx].entityID, scene.get());
        if (view.contains(ent)) {
            auto &materialComp = view.get<MaterialComponent>(ent);
            if (materialComp.material) {
                m_entityToOffset[ent.getID()] = idx * static_cast<uint32_t>(sizeof(RtInstanceInfo));
                m_materialToOffsets[materialComp.material.get()].push_back(idx * static_cast<uint32_t>(sizeof(RtInstanceInfo)));
            }
        }
    }

    auto set = DescriptorManager::getDescriptorSet(DescriptorSetBindingLocation::RT_SCENE_INFO_SSBOS);
    if (set) {
        auto binding = set->getSSBOBinding(DescriptorSetBindingLocation::RT_SCENE_INFO_SSBOS);
        if (binding) {
            m_meshDataSSBOIndex = binding->add(*m_buffer);
        }
    }

    m_dirtyMaterials.clear();
    m_dirtyTransforms.clear();

    RP_CORE_INFO("RtInstanceData: rebuilt {} instances", infos.size());
}

void RtInstanceData::patchDirty(std::shared_ptr<Scene> scene)
{
    if (m_dirtyMaterials.empty() && m_dirtyTransforms.empty()) {
        return;
    }

    if (!m_buffer) return;

    constexpr size_t MAT_START = offsetof(RtInstanceInfo, AlbedoTextureIndex);
    constexpr size_t MAT_END = offsetof(RtInstanceInfo, iboIndex);
    constexpr size_t MAT_SIZE = MAT_END - MAT_START;
    constexpr size_t TRANSFORM_OFFSET = offsetof(RtInstanceInfo, modelMatrix);

    struct PackedMat {
        uint32_t AlbedoTextureIndex;
        uint32_t NormalTextureIndex;
        alignas(16) glm::vec3 albedo;
        alignas(16) glm::vec3 emissiveColor;
        uint32_t EmissiveFactorTextureIndex;
    };

    for (auto *mat : m_dirtyMaterials) {
        if (!mat) continue;

        auto it = m_materialToOffsets.find(mat);
        if (it == m_materialToOffsets.end()) continue;

        PackedMat packed = {};
        packed.AlbedoTextureIndex = mat->getParameter(ParameterID::ALBEDO_MAP).asUInt();
        packed.NormalTextureIndex = mat->getParameter(ParameterID::NORMAL_MAP).asUInt();
        packed.albedo = mat->getParameter(ParameterID::ALBEDO).asVec3();
        packed.emissiveColor = mat->getParameter(ParameterID::EMISSIVE).asVec3();
        packed.EmissiveFactorTextureIndex = mat->getParameter(ParameterID::EMISSIVE_MAP).asUInt();

        for (uint32_t baseOffset : it->second) {
            uint32_t dst = baseOffset + static_cast<uint32_t>(MAT_START);
            m_buffer->addData(&packed, static_cast<uint32_t>(MAT_SIZE), dst);
        }
    }

    // auto &reg = scene->getRegistry();
    for (uint32_t entID : m_dirtyTransforms) {
        auto it = m_entityToOffset.find(entID);
        if (it == m_entityToOffset.end()) continue;

        Entity ent = Entity(entID, scene.get());
        if (!ent.hasComponent<TransformComponent>()) continue;

        uint32_t dst = it->second + static_cast<uint32_t>(TRANSFORM_OFFSET);
        glm::mat4 model = ent.getComponent<TransformComponent>().transformMatrix();
        m_buffer->addData(&model, sizeof(glm::mat4), dst);
    }

    m_dirtyMaterials.clear();
    m_dirtyTransforms.clear();
}

} // namespace Rapture
