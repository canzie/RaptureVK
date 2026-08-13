#include "WorldSettingsPanel.h"

#include "asset_manager/Asset.h"
#include "scenes/World.h"
#include "scenes/instances/Node3D.h"
#include "scenes/instances/controllers/Controller.h"

#include <components/common.h>

// a puppet is any scene object with a place in the world, which is all a controller can drive
static bool s_isPuppetAsset(Rapture::AssetHandle handle, const Rapture::AssetMetadata &metadata)
{
    (void)handle;
    return metadata.authoredClass != nullptr && metadata.authoredClass->isA(Rapture::Node3D::staticType());
}

static bool s_isControllerAsset(Rapture::AssetHandle handle, const Rapture::AssetMetadata &metadata)
{
    (void)handle;
    return metadata.authoredClass != nullptr && metadata.authoredClass->isA(Rapture::Controller::staticType());
}

void WorldPlaySection::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowAssetPicker(t, "Puppet", m_puppetPicker,
                       {.types = {Rapture::ASSET_SCENE_OBJECT}, .predicate = s_isPuppetAsset},
                       [this](Rapture::AssetHandle handle) {
                           if (world != nullptr) {
                               world->data().puppet = handle;
                           }
                       });
        rowAssetPicker(t, "Controller", m_controllerPicker,
                       {.types = {Rapture::ASSET_SCENE_OBJECT}, .predicate = s_isControllerAsset},
                       [this](Rapture::AssetHandle handle) {
                           if (world != nullptr) {
                               world->data().controller = handle;
                           }
                       });
    });
}

void WorldPlaySection::sync()
{
    if (world == nullptr) {
        return;
    }

    if (m_puppetPicker.has_value()) {
        m_puppetPicker->setAsset(world->data().puppet);
    }
    if (m_controllerPicker.has_value()) {
        m_controllerPicker->setAsset(world->data().controller);
    }
}

WorldSettingsPanel::WorldSettingsPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context)
    : Panel("World Settings", context), m_world(context.world)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        m_placeholderText = nullptr;
    });
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    setupPlaceholder();
    setupWorldView();

    icon = Icons::SVG_WORLD;
    attach(tabBar, std::move(root));

    m_placeholderText->setBaseProperties({.visible = m_world == nullptr});
    m_sections->setVisible(m_world != nullptr);
    refresh();
}

WorldSettingsPanel::~WorldSettingsPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void WorldSettingsPanel::setupPlaceholder()
{
    Amethyst::UIScope(*m_root).textLabel(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, 0.0f),
                },
            .style = {.backgroundTransparency = 1.0f},
            .text =
                {
                    .textColor = Amethyst::Color4(0.5f, 0.5f, 0.5f, 1.0f),
                    .textXAlignment = Amethyst::TextXAlignment::CENTER,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = "No world open",
        },
        [this](Amethyst::TextLabelScope &tl) { m_placeholderText = &tl.component; });
}

void WorldSettingsPanel::setupWorldView()
{
    m_sections.emplace(*m_root, Amethyst::ScrollingFrameProperties{
                                    .classes = {"panel"},
                                    .base =
                                        {
                                            .clipsDescendants = true,
                                            .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                                            .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, 0.0f),
                                            .visible = false,
                                        },
                                    .scroll =
                                        {
                                            .scrollAxis = Amethyst::ScrollAxis::Y,
                                            .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                                            .canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
                                        },
                                });
}

void WorldSettingsPanel::refresh()
{
    m_sections->refresh([this]() {
        if (WorldPlaySection *section = m_sections->ensure<WorldPlaySection>(m_world != nullptr)) {
            section->world = m_world;
        }
    });
}

void WorldSettingsPanel::onUpdate(float dt)
{
    (void)dt;
    if (m_world == nullptr) {
        return;
    }
    m_sections->sync();
}
