#include "AssetDetailsPanel.h"

#include "Icons.h"

#include "EntitySelection.h"

#include <rapture/assets/asset_manager/AssetManager.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/instances/Bone3D.h>

#include <utility>

AssetDetailsPanel::AssetDetailsPanel(const WorkspaceContext &context, Rapture::AssetHandle handle)
    : Panel("Details", context), m_handle(handle)
{
    m_icon = Icons::SVG_PROPERTIES;
}

AssetDetailsPanel::AssetDetailsPanel(const WorkspaceContext &context, Rapture::AssetHandle handle, MeshBakeTransform &bakeTransform)
    : AssetDetailsPanel(context, handle)
{
    m_bakeTransform = &bakeTransform;
}

void AssetDetailsPanel::build(Amethyst::UIScope &scope)
{
    (void)scope;
    setupSectionView();

    if (m_selection != nullptr) {
        m_selectionChangedConn = m_selection->onChanged.connect([this](Rapture::ecs::EntityAccessor entity) {
            (void)entity;
            m_selectionChanged = true;
        });
    }

    refresh();
}

void AssetDetailsPanel::setupSectionView()
{
    m_sections.emplace(*m_root, Amethyst::ScrollingFrameProperties{
                                    .classes = {"panel"},
                                    .base =
                                        {
                                            .clipsDescendants = true,
                                            .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, 0.0f),
                                        },
                                    .scroll =
                                        {
                                            .scrollAxis = Amethyst::ScrollAxis::Y,
                                            .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                                            .canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
                                        },
                                });
}

void AssetDetailsPanel::refresh()
{
    m_sections->refresh([this]() {
        const Rapture::AssetType type = Rapture::AssetManager::getAssetMetadata(m_handle).assetType;
        const bool isMesh = type == Rapture::ASSET_STATIC_MESH || type == Rapture::ASSET_SKELETAL_MESH;

        Rapture::Bone3D *bone = type == Rapture::ASSET_SKELETON ? selectedBone() : nullptr;
        m_sections->ensurePrepared<SkeletonJointAssetEditor>(bone != nullptr, [this, bone](SkeletonJointAssetEditor &editor) {
            editor.setSubject(m_handle);
            editor.setBone(bone);
        });
        ensure<MeshAssetEditor>(isMesh);
        if (m_bakeTransform != nullptr) {
            m_sections->ensurePrepared<MeshTransformAssetEditor>(
                isMesh, [this](MeshTransformAssetEditor &editor) { editor.setSubject(m_handle); }, *m_bakeTransform);
        }
        ensure<MeshImportAssetEditor>(isMesh);
        ensure<MeshMaterialAssetEditor>(isMesh);
        ensure<MeshSkeletonAssetEditor>(type == Rapture::ASSET_SKELETAL_MESH);
        ensure<SkeletonAssetEditor>(type == Rapture::ASSET_SKELETON);
        ensure<SkeletonPreviewAssetEditor>(type == Rapture::ASSET_SKELETON);
        ensure<TextureAssetEditor>(type == Rapture::ASSET_TEXTURE);
        ensure<TextureSamplingAssetEditor>(type == Rapture::ASSET_TEXTURE);
        ensure<TextureImportAssetEditor>(type == Rapture::ASSET_TEXTURE);
    });
}

void AssetDetailsPanel::onUpdate(float dt)
{
    (void)dt;

    const bool selectionChanged = std::exchange(m_selectionChanged, false);
    if (selectionChanged) {
        // the joint's name is its section's title, so another joint gets a section of its own
        m_sections->ensure<SkeletonJointAssetEditor>(false);
    }

    if (m_sections->consumeRefreshRequest() || selectionChanged) {
        refresh();
    }

    m_sections->sync();
}

Rapture::Bone3D *AssetDetailsPanel::selectedBone() const
{
    if (m_selection == nullptr || !m_selection->has() || m_context.scene == nullptr) {
        return nullptr;
    }

    Rapture::SceneObject *object = m_context.scene->instanceFor(m_selection->entity().getEntity());
    return object != nullptr ? object->as<Rapture::Bone3D>() : nullptr;
}
