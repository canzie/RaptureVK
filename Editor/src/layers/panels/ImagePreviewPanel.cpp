#include "ImagePreviewPanel.h"

#include "asset_manager/Asset.h"
#include "asset_manager/AssetManager.h"
#include "logging/Log.h"
#include "textures/Texture.h"

#include <components/context_menu_item.h>
#include <components/ui_scope.h>

static constexpr float PICKER_HEADER_HEIGHT = 34.0f;

ImagePreviewPanel::ImagePreviewPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context, std::string title,
                                     ImagePreviewMode mode)
    : Panel(title, context), m_mode(mode)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });
    m_root->name = std::string(title);
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    const bool picker = m_mode == ImagePreviewMode::ASSET_PICKER;
    const float imageOffsetY = picker ? PICKER_HEADER_HEIGHT : 0.0f;

    Amethyst::UIScope scope(*m_root);

    if (picker) {
        scope.frame(
            {
                .classes = {"panel"},
                .base = {.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, PICKER_HEADER_HEIGHT)},
            },
            [this](Amethyst::FrameScope &f) {
                f.dropdown(
                    {
                        .base = {.position = Amethyst::UDim2::fromOffset(4.0f, 4.0f),
                                 .size = Amethyst::UDim2::fromOffset(240.0f, 26.0f)},
                        .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
                        .label = "Select texture...",
                    },
                    [this](Amethyst::DropdownScope &d) { m_selector = &d.component; });
                f.textButton(
                    {
                        .base = {.position = Amethyst::UDim2::fromOffset(248.0f, 4.0f),
                                 .size = Amethyst::UDim2::fromOffset(70.0f, 26.0f)},
                        .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER,
                                 .textYAlignment = Amethyst::TextYAlignment::CENTER},
                        .label = "Refresh",
                    },
                    [this](Amethyst::TextButtonScope &b) {
                        b.component.onMouseButton1ClickCb = [this]() {
                            rebuildSelector();
                            return Amethyst::EventResult::CONSUMED;
                        };
                    });
            });
    }

    scope.imageLabel(
        {
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, imageOffsetY),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -imageOffsetY),
                },
            .style = {.backgroundTransparency = 1.0f},
            .image = {.imageColor = Amethyst::Color4(1.0f, 1.0f, 1.0f, 1.0f)},
        },
        [this](Amethyst::ImageLabelScope &img) {
            m_image = &img.component;
            m_image->setSvg(
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><circle cx="50" cy="50" r="40" fill="#ff6600"/></svg>)");
        });

    if (picker) {
        rebuildSelector();
    }

    attach(tabBar, std::move(root));
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

void ImagePreviewPanel::rebuildSelector()
{
    if (m_selector == nullptr) {
        return;
    }

    auto handles = Rapture::AssetManager::getVirtualAssetsByType(Rapture::AssetType::TEXTURE);

    std::vector<std::unique_ptr<Amethyst::ContextMenu::ItemData>> items;
    items.reserve(handles.size());
    for (Rapture::AssetHandle handle : handles) {
        std::string name = Rapture::AssetManager::getAssetMetadata(handle).getName();
        items.push_back(Amethyst::makeActionItem(name, [this, handle]() { selectTexture(handle); }));
    }
    m_selector->setItems(std::move(items));
}

void ImagePreviewPanel::selectTexture(Rapture::AssetHandle handle)
{
    if (!m_services.registerTexture) {
        RP_ERROR("registerTexture service not set, cannot display texture");
        return;
    }

    auto assetRef = Rapture::AssetManager::getAsset(handle);
    if (!assetRef) {
        return;
    }
    auto *texture = assetRef.get()->getUnderlyingAsset<Rapture::Texture>();
    if (texture == nullptr) {
        return;
    }

    auto it = m_registered.find(handle);
    if (it == m_registered.end()) {
        it = m_registered.emplace(handle, m_services.registerTexture(texture)).first;
    }

    setImage(it->second);
    if (m_selector != nullptr) {
        m_selector->setText(Rapture::AssetManager::getAssetMetadata(handle).getName());
    }
}
