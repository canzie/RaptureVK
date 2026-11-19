#include "TextureWorkspace.h"

#include "panels/AssetDetailsPanel.h"
#include "panels/TextureCanvasPanel.h"
#include "ui/AssetVisuals.h"

static constexpr float DETAILS_SPLIT = 0.75f;

TextureWorkspace::TextureWorkspace(const EditorContext &context, Rapture::AssetHandle handle) : AssetWorkspace(context, handle) {}

std::string_view TextureWorkspace::tabIcon() const
{
    return Asset_iconForType(Rapture::ASSET_TEXTURE);
}

void TextureWorkspace::build()
{
    Amethyst::TabBar *canvasTabBar = nullptr;
    Amethyst::TabBar *detailsTabBar = nullptr;
    Amethyst::DockScope(*m_contentDock)
        .split(
            Amethyst::SplitAxis::VERTICAL, DETAILS_SPLIT,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { canvasTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) { r.panel([&](Amethyst::TabBarScope &tb) { detailsTabBar = &tb.component; }); });

    addPanel(std::make_unique<TextureCanvasPanel>(m_context, handle()), canvasTabBar);
    addPanel(std::make_unique<AssetDetailsPanel>(m_context, handle()), detailsTabBar);
}
