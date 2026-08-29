#include "RtInstanceData.h"

#include "app/Application.h"
#include "assets/materials/MaterialInstance.h"
#include "assets/materials/MaterialParameters.h"
#include "assets/meshes/Mesh.h"
#include "core/ecs/entity_accessor.h"
#include "core/events/AssetEvents.h"
#include "core/utils/Log.h"
#include "gpu/acceleration_structures/TLAS.h"
#include "gpu/buffers/BufferLayout.h"
#include "gpu/descriptors/DescriptorManager.h"
#include "gpu/descriptors/DescriptorSet.h"
#include "scene/components/Components.h"

namespace Rapture {

RtInstanceData::RtInstanceData(const RenderContext &renderContext)
    : m_rc(renderContext), m_allocator(renderContext.vulkanContext->getVmaAllocator())
{

    AssetEvents::onMaterialInstanceChanged().addListener([this](MaterialInstance *mat) {
        if (mat) m_dirtyMaterials.insert(mat);
    });
}

RtInstanceData::~RtInstanceData() {}

void RtInstanceData::markMaterialDirty(MaterialInstance *material)
{
    if (material) m_dirtyMaterials.insert(material);
}

void RtInstanceData::update(Scene &scene)
{
    auto tlas = scene.getTLAS();
    if (!tlas || !tlas->isBuilt() || tlas->getInstanceCount() == 0) {
        return;
    }

    if (m_tlasRevision != tlas->getRevision()) {
        rebuild(scene);
    } else {
        patchDirty(scene);
    }
}

void RtInstanceData::rebuild(Scene &scene)
{
    TLAS *tlas = scene.getTLAS();
    const auto &tlasInstances = tlas->getInstances();
    ecs::Registry &reg = scene.getRegistry();

    auto isDrawable = [&reg](ecs::Entity entity) {
        return reg.hasAll<MaterialComponent, StaticMeshComponent, TransformComponent>(entity);
    };

    // a shader reaches an entry by the slot the structure gave the instance, so removed instances leave holes
    std::vector<RtInstanceInfo> infos(tlas->getSlotCapacity());
    std::vector<RtGeometryInfo> geometries;

    m_materialToOffsets.clear();
    m_entityToOffset.clear();

    for (const TLASInstance &instance : tlasInstances) {
        if (instance.slot >= infos.size() || !isDrawable(instance.entityId)) {
            continue;
        }

        RtInstanceInfo &info = infos[instance.slot];

        const StaticMeshComponent &meshComp = reg.read<StaticMeshComponent>(instance.entityId);
        const MaterialComponent &materialComp = reg.read<MaterialComponent>(instance.entityId);

        info.modelMatrix = reg.read<TransformComponent>(instance.entityId).world;

        // the runs are laid out in the order the acceleration structure holds its geometries, so a
        // hit's geometry index reaches its own run's entry
        info.firstGeometry = static_cast<uint32_t>(geometries.size());
        if (meshComp.mesh) {
            for (uint32_t slot = 0; slot < meshComp.mesh->geometry().getSections().size(); slot++) {
                MaterialInstance *runMaterial = materialComp.materialAt(slot);
                uint32_t runMaterialIndex = runMaterial != nullptr ? runMaterial->getBindlessIndex() : 0;

                if (runMaterial != nullptr) {
                    m_materialToOffsets[runMaterial].push_back(static_cast<uint32_t>(geometries.size()) *
                                                               static_cast<uint32_t>(sizeof(RtGeometryInfo)));
                }

                geometries.push_back({runMaterialIndex, meshComp.mesh->geometry().getSections()[slot].firstIndex});
            }
        }

        if (meshComp.mesh) {
            auto vb = meshComp.mesh->geometry().getVertexBuffer();
            auto ib = meshComp.mesh->geometry().getIndexBuffer();

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

        m_entityToOffset[instance.entityId] = instance.slot * static_cast<uint32_t>(sizeof(RtInstanceInfo));
    }

    m_instanceCount = static_cast<uint32_t>(infos.size());
    m_tlasRevision = tlas->getRevision();

    if (geometries.empty()) {
        geometries.push_back({});
    }

    if (!m_buffer || m_buffer->getSize() < sizeof(RtInstanceInfo) * infos.size()) {
        m_buffer =
            std::make_shared<StorageBuffer>(sizeof(RtInstanceInfo) * infos.size(), BufferUsage::DYNAMIC, m_allocator, infos.data());
    } else {
        m_buffer->addData(infos.data(), sizeof(RtInstanceInfo) * infos.size(), 0);
    }

    if (!m_geometryBuffer || m_geometryBuffer->getSize() < sizeof(RtGeometryInfo) * geometries.size()) {
        m_geometryBuffer = std::make_shared<StorageBuffer>(sizeof(RtGeometryInfo) * geometries.size(), BufferUsage::DYNAMIC,
                                                           m_allocator, geometries.data());
    } else {
        m_geometryBuffer->addData(geometries.data(), sizeof(RtGeometryInfo) * geometries.size(), 0);
    }

    auto set = m_rc.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::RT_SCENE_INFO_SSBOS);
    if (set) {
        auto binding = set->getSSBOBinding(DescriptorSetBindingLocation::RT_SCENE_INFO_SSBOS);
        if (binding) {
            m_meshDataSSBOIndex = binding->add(*m_buffer);
        }
    }

    auto geometrySet = m_rc.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::RT_GEOMETRY_INFO_SSBOS);
    if (geometrySet) {
        auto binding = geometrySet->getSSBOBinding(DescriptorSetBindingLocation::RT_GEOMETRY_INFO_SSBOS);
        if (binding) {
            m_geometryDataSSBOIndex = binding->add(*m_geometryBuffer);
        }
    }

    m_dirtyMaterials.clear();

    // a rebuild wrote every transform, so the bookmark advances past everything recorded so far
    scene.getRegistry().getJournal().readSince(CHANNEL_TRANSFORM_WORLD, m_transformBookmark);

    RP_CORE_INFO("RtInstanceData: rebuilt {} instances", infos.size());
}

void RtInstanceData::patchDirty(Scene &scene)
{
    if (!m_buffer) return;

    constexpr size_t MAT_START = offsetof(RtGeometryInfo, materialIndex);
    constexpr size_t TRANSFORM_OFFSET = offsetof(RtInstanceInfo, modelMatrix);

    for (auto *mat : m_dirtyMaterials) {
        if (!mat || m_geometryBuffer == nullptr) continue;

        auto it = m_materialToOffsets.find(mat);
        if (it == m_materialToOffsets.end()) continue;

        uint32_t materialIndex = mat->getBindlessIndex();

        for (uint32_t baseOffset : it->second) {
            uint32_t dst = baseOffset + static_cast<uint32_t>(MAT_START);
            m_geometryBuffer->addData(&materialIndex, sizeof(uint32_t), dst);
        }
    }

    ecs::Registry &reg = scene.getRegistry();
    ecs::Batch changed = reg.getJournal().readSince(CHANNEL_TRANSFORM_WORLD, m_transformBookmark);

    if (changed.needsRebuild()) {
        rebuild(scene);
        return;
    }

    for (ecs::Entity entity : changed) {
        // most of what changed is not in the acceleration structure
        auto it = m_entityToOffset.find(entity);
        if (it == m_entityToOffset.end()) continue;

        const TransformComponent *transform = reg.tryRead<TransformComponent>(entity);
        if (transform == nullptr) continue;

        uint32_t dst = it->second + static_cast<uint32_t>(TRANSFORM_OFFSET);
        glm::mat4 model = transform->world;
        m_buffer->addData(&model, sizeof(glm::mat4), dst);
    }

    m_dirtyMaterials.clear();
}

} // namespace Rapture
