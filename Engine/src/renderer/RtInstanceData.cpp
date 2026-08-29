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

        if (materialComp.material) {
            info.materialIndex = materialComp.material->getBindlessIndex();
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

        if (materialComp.material) {
            uint32_t offset = instance.slot * static_cast<uint32_t>(sizeof(RtInstanceInfo));
            m_entityToOffset[instance.entityId] = offset;
            m_materialToOffsets[materialComp.material.operator->()].push_back(offset);
        }
    }

    m_instanceCount = static_cast<uint32_t>(infos.size());
    m_tlasRevision = tlas->getRevision();

    if (!m_buffer || m_buffer->getSize() < sizeof(RtInstanceInfo) * infos.size()) {
        m_buffer =
            std::make_shared<StorageBuffer>(sizeof(RtInstanceInfo) * infos.size(), BufferUsage::DYNAMIC, m_allocator, infos.data());
    } else {
        m_buffer->addData(infos.data(), sizeof(RtInstanceInfo) * infos.size(), 0);
    }

    auto set = m_rc.descriptorManager->getDescriptorSet(DescriptorSetBindingLocation::RT_SCENE_INFO_SSBOS);
    if (set) {
        auto binding = set->getSSBOBinding(DescriptorSetBindingLocation::RT_SCENE_INFO_SSBOS);
        if (binding) {
            m_meshDataSSBOIndex = binding->add(*m_buffer);
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

    constexpr size_t MAT_START = offsetof(RtInstanceInfo, materialIndex);
    constexpr size_t MAT_END = offsetof(RtInstanceInfo, iboIndex);
    constexpr size_t MAT_SIZE = MAT_END - MAT_START;
    constexpr size_t TRANSFORM_OFFSET = offsetof(RtInstanceInfo, modelMatrix);

    struct PackedMat {
        uint32_t materialIndex;
    };

    for (auto *mat : m_dirtyMaterials) {
        if (!mat) continue;

        auto it = m_materialToOffsets.find(mat);
        if (it == m_materialToOffsets.end()) continue;

        PackedMat packed = {};
        packed.materialIndex = mat->getBindlessIndex();

        for (uint32_t baseOffset : it->second) {
            uint32_t dst = baseOffset + static_cast<uint32_t>(MAT_START);
            m_buffer->addData(&packed, static_cast<uint32_t>(MAT_SIZE), dst);
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
