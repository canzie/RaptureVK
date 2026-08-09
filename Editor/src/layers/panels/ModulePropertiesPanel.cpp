#include "ModulePropertiesPanel.h"

#include "Icons.h"
#include "asset_manager/AssetManager.h"
#include "layers/panels/module_editors/ModuleEditors.h"
#include "logging/Log.h"

#include <components/common.h>

ModulePropertiesPanel::ModulePropertiesPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context,
                                             Rapture::AssetHandle module)
    : Panel("Details", context)
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
    setupModuleView();

    icon = Icons::SVG_PROPERTIES;
    attach(tabBar, std::move(root));

    loadModule(module);
}

ModulePropertiesPanel::~ModulePropertiesPanel()
{
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void ModulePropertiesPanel::setupPlaceholder()
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
            .label = "No module open",
        },
        [this](Amethyst::TextLabelScope &tl) { m_placeholderText = &tl.component; });
}

void ModulePropertiesPanel::setupModuleView()
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

void ModulePropertiesPanel::loadModule(Rapture::AssetHandle handle)
{
    m_moduleRef = Rapture::AssetManager::getAsset(handle);
    m_module = m_moduleRef ? m_moduleRef.get()->getUnderlyingAsset<Rapture::ModuleClass>() : nullptr;

    if (m_module == nullptr) {
        RP_ERROR("Asset {} holds no module", static_cast<uint64_t>(handle));
        showPlaceholder();
        refresh();
        return;
    }

    m_placeholderText->setBaseProperties({.visible = false});
    m_sections->setVisible(true);
    refresh();
}

void ModulePropertiesPanel::refresh()
{
    m_sections->refresh([this]() {
        if (m_module == nullptr) {
            return;
        }

        // Sections come from the module's class chain, base first, so a base class sits above the
        // sections its subclasses add.
        ensure<CameraControllerEditor>(m_module->isA<Rapture::CameraController>());
        ensure<PlayerControllerEditor>(m_module->isA<Rapture::PlayerController>());
    });
}

void ModulePropertiesPanel::showPlaceholder()
{
    if (m_placeholderText == nullptr) {
        return;
    }
    m_placeholderText->setBaseProperties({.visible = true});
    m_sections->setVisible(false);
}

void ModulePropertiesPanel::onUpdate(float dt)
{
    (void)dt;
    if (m_module == nullptr) {
        return;
    }
    m_sections->sync();
}
