#include "renderer/SceneGeometryDraw.h"

#include "components/Components.h"
#include "logging/Log.h"
#include "logging/TracyProfiler.h"
#include "meshes/Mesh.h"
#include "renderer/Frustum.h"
#include "scenes/Scene.h"
#include "window_context/Application.h"

namespace Rapture {

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

    for (auto [entity, transform, meshComp, materialComp] :
         registry.read<TransformComponent, MeshComponent, MaterialComponent>()) {
        RAPTURE_PROFILE_SCOPE("Populate Batch");

        if (!meshComp.mesh || meshComp.isLoading) {
            continue;
        }

        auto mesh = meshComp.mesh;
        if (!mesh->getVertexBuffer() || !mesh->getIndexBuffer()) {
            continue;
        }

        // a bounding box derived from the transform is a cache, not a change to the mesh
        registry.write<MeshComponent>(entity, 0)->updateWorldBoundingBox(transform);

        if (frustum != nullptr && frustum->testBoundingBox(meshComp.worldBoundingBox) == FrustumResult::Outside) {
            continue;
        }

        auto vboAlloc = mesh->getVertexAllocation();
        auto iboAlloc = mesh->getIndexAllocation();
        if (!vboAlloc || !iboAlloc) {
            continue;
        }

        MDIBatch *batch = batchMap.obtainBatch(vboAlloc, iboAlloc, mesh->getVertexBuffer()->getBufferLayout(),
                                               mesh->getIndexBuffer()->getIndexType());

        uint32_t meshSlotIndex = meshComp.renderDataSlot;
        uint32_t materialIndex = materialComp.material ? materialComp.material->getBindlessIndex() : 0;

        batch->addObject(*mesh, meshSlotIndex, materialIndex);
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
