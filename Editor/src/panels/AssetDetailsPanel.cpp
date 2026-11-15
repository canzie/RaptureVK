#include "AssetDetailsPanel.h"

#include "Icons.h"
#include "layers/panels/asset_editors/AssetEditors.h"

#include <assets/asset_manager/AssetManager.h>

AssetDetailsPanel::AssetDetailsPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, Rapture::AssetHandle handle)
    : Panel("Details", context), m_handle(handle)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    setupSectionView();

    icon = Icons::SVG_PROPERTIES;
    attach(tabBar, std::move(root));

    refresh();
}

AssetDetailsPanel::~AssetDetailsPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
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
