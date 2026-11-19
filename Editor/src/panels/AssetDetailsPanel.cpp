#include "AssetDetailsPanel.h"

#include "Icons.h"
#include "inspector/AssetEditors.h"

#include <rapture/assets/asset_manager/AssetManager.h>

AssetDetailsPanel::AssetDetailsPanel(const WorkspaceContext &context, Rapture::AssetHandle handle)
    : Panel("Details", context), m_handle(handle)
{
    m_icon = Icons::SVG_PROPERTIES;
}

void AssetDetailsPanel::build(Amethyst::UIScope &scope)
{
    (void)scope;
    setupSectionView();
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

        ensure<MeshAssetEditor>(isMesh);
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

    if (m_sections->consumeRefreshRequest()) {
        refresh();
    }

    m_sections->sync();
}
