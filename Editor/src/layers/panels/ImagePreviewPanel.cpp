#include "ImagePreviewPanel.h"

#include "layers/panels/components/tab_layouts.h"

#include <components/ui_scope.h>

ImagePreviewPanel::ImagePreviewPanel(Amethyst::TabBar *tabBar, const PanelServices &services, std::string_view title)
    : Panel(services)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });
    m_root->name = std::string(title);
    m_root->addClass("background-secondary");
    m_root->setBaseProperties({.clipsDescendants = true});

    Amethyst::UIScope(*m_root).imageLabel(
        {
            .base =
                {
                    .anchorPoint = Amethyst::vec2(0.5f, 0.5f),
                    .position = Amethyst::UDim2::fromScale(0.5f, 0.5f),
                    .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
                },
            .style = {.backgroundTransparency = 1.0f},
            .image = {.imageColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f)},
        },
        [this](Amethyst::ImageLabelScope &img) {
            m_image = &img.component;
            m_image->setSvg(
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><circle cx="50" cy="50" r="40" fill="#ff6600"/></svg>)");
        });

    tabBar->addTab(std::move(root), iconTabLayout(title));
}

ImagePreviewPanel::~ImagePreviewPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void ImagePreviewPanel::setImage(Amethyst::AmTextureId image)
{
    if (m_image != nullptr) {
        m_image->setImage(image);
    }
}

void ImagePreviewPanel::clearImage()
{
    if (m_image != nullptr) {
        m_image->setImage(Amethyst::AmTextureId{});
    }
}
