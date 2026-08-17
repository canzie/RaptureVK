#include "renderer/SceneGeometryDraw.h"

#include "app/Application.h"
#include "assets/meshes/Mesh.h"
#include "core/utils/Log.h"
#include "core/utils/TracyProfiler.h"
#include "renderer/Frustum.h"
#include "scene/Scene.h"
#include "scene/components/Components.h"
#include "scene/render_data/SceneRenderData.h"

namespace Rapture {

static void s_addMeshToBatch(MDIBatchMap &batchMap, Mesh &mesh, uint32_t meshSlotIndex, uint32_t materialIndex)
{
    auto vboAlloc = mesh.getVertexAllocation();
    auto iboAlloc = mesh.getIndexAllocation();
    if (!vboAlloc || !iboAlloc) {
        return;
    }

    MDIBatch *batch = batchMap.obtainBatch(vboAlloc, iboAlloc, mesh.getVertexBuffer()->getBufferLayout(),
                                           mesh.getIndexBuffer()->getIndexType());

    batch->addObject(mesh, meshSlotIndex, materialIndex);
}

SceneGeometryDraw::SceneGeometryDraw(RenderContext renderContext, uint32_t framesInFlight) : m_rc(renderContext)
{
    m_batchMaps.resize(framesInFlight);
    for (uint32_t frame = 0; frame < framesInFlight; frame++) {
        m_batchMaps[frame] = std::make_unique<MDIBatchMap>(m_rc);
    }

    m_populatedBatches.resize(framesInFlight);
}

void SceneGeometryDraw::populate(Scene &scene, const Frustum *frustum, uint32_t frameInFlight)
{
    RAPTURE_PROFILE_FUNCTION();

    if (frameInFlight >= m_batchMaps.size()) {
        RP_CORE_ERROR("Frame {} has no batches", frameInFlight);
        return;
    }

    MDIBatchMap &batchMap = *m_batchMaps[frameInFlight];
    batchMap.beginFrame();

    std::vector<MDIBatch *> &populated = m_populatedBatches[frameInFlight];
    populated.clear();

    auto &registry = scene.getRegistry();
    SceneRenderData *renderData = scene.getRenderData();

    for (auto [entity, transform, meshComp, materialComp] : registry.read<TransformComponent, StaticMeshComponent, MaterialComponent>()) {
        RAPTURE_PROFILE_SCOPE("Populate Batch");

        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }

        auto mesh = meshComp.mesh;
        if (!mesh->getVertexBuffer() || !mesh->getIndexBuffer()) {
            continue;
        }

        // a bounding box derived from the transform is a cache, not a change to the mesh
        registry.write<StaticMeshComponent>(entity, 0)->updateWorldBoundingBox(transform);

        if (frustum != nullptr && frustum->testBoundingBox(meshComp.worldBoundingBox) == FrustumResult::Outside) {
            continue;
        }

        uint32_t materialIndex = materialComp.material ? materialComp.material->getBindlessIndex() : 0;

        s_addMeshToBatch(batchMap, *mesh, renderData->getMeshSlot(entity), materialIndex);
    }

    for (auto [entity, transform, meshComp, materialComp] :
         registry.read<TransformComponent, SkeletalMeshComponent, MaterialComponent>()) {
        RAPTURE_PROFILE_SCOPE("Populate Batch");

        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }

        auto mesh = meshComp.mesh;
        if (!mesh->getVertexBuffer() || !mesh->getIndexBuffer()) {
            continue;
        }

        // TODO: bind pose bounds, so an animated mesh whose joints leave them culls too early
        registry.write<SkeletalMeshComponent>(entity, 0)->updateWorldBoundingBox(transform);

        if (frustum != nullptr && frustum->testBoundingBox(meshComp.worldBoundingBox) == FrustumResult::Outside) {
            continue;
        }

        uint32_t materialIndex = materialComp.material ? materialComp.material->getBindlessIndex() : 0;

        s_addMeshToBatch(batchMap, *mesh, renderData->getMeshSlot(entity), materialIndex);
    }

    for (const auto &[batchKey, batch] : batchMap.getBatches()) {
        if (batch->getDrawCount() == 0) {
            continue;
        }
        populated.push_back(batch.get());
    }
}

std::span<MDIBatch *const> SceneGeometryDraw::batches(uint32_t frameInFlight) const
{
    if (frameInFlight >= m_populatedBatches.size()) {
        RP_CORE_ERROR("Frame {} has no batches", frameInFlight);
        return {};
    }

    return m_populatedBatches[frameInFlight];
}

void SceneGeometryDraw::bindBatch(CommandBuffer *commandBuffer, MDIBatch *batch)
{
    RAPTURE_PROFILE_FUNCTION();

    batch->uploadBuffers();

    auto &vc = Application::getInstance().getVulkanContext();
    VkCommandBuffer cmd = commandBuffer->getCommandBufferVk();

    auto bindingDescription = batch->getBufferLayout().getBindingDescription2EXT();
    auto attributeDescriptions = batch->getBufferLayout().getAttributeDescriptions2EXT();
    vc.vkCmdSetVertexInputEXT(cmd, 1, &bindingDescription, static_cast<uint32_t>(attributeDescriptions.size()),
                              attributeDescriptions.data());

    VkBuffer vertexBuffer = batch->getVertexBuffer();
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vertexOffset);

    vkCmdBindIndexBuffer(cmd, batch->getIndexBuffer(), 0, batch->getIndexType());
}

} // namespace Rapture
