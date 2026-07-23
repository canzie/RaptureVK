#include "TextureGeneratorWorkspace.h"

#include "asset_manager/AssetManager.h"
#include "layers/panels/ImagePreviewPanel.h"
#include "layers/panels/ShaderInputsPanel.h"
#include "layers/panels/components/tab_layouts.h"
#include "logging/Log.h"
#include "shaders/Shader.h"
#include "shaders/ShaderCommon.h"

#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>

#include <string>

static void s_addPlaceholderTab(Amethyst::TabBar *tabBar, std::string_view name)
{
    tabBar->addClass("panel-tab");
    auto root = std::make_unique<Amethyst::Frame>();
    root->name = std::string(name);
    root->addClass("panel");
    root->setBaseProperties({.clipsDescendants = true});
    tabBar->addTab(std::move(root), iconTabLayout(name));
}

TextureGeneratorWorkspace::TextureGeneratorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services)
{
    m_context.services = services;
    setupBase(tabs, "Texture Generator");
    m_dockingLayer->name = "Texture Generator Dock";
    m_dockingLayer->tabBarClasses = {"panel-tab"};

    Amethyst::TabBar *inputsTabBar = nullptr;
    Amethyst::TabBar *previewTabBar = nullptr;
    Amethyst::TabBar *sourceTabBar = nullptr;

    Amethyst::DockScope(*m_dockingLayer)
        .split(
            Amethyst::SplitAxis::VERTICAL, 0.28f,
            [&](Amethyst::DockScope &l) { l.panel([&](Amethyst::TabBarScope &tb) { inputsTabBar = &tb.component; }); },
            [&](Amethyst::DockScope &r) {
                r.split(
                    Amethyst::SplitAxis::HORIZONTAL, 0.68f,
                    [&](Amethyst::DockScope &t) { t.panel([&](Amethyst::TabBarScope &tb) { previewTabBar = &tb.component; }); },
                    [&](Amethyst::DockScope &b) { b.panel([&](Amethyst::TabBarScope &tb) { sourceTabBar = &tb.component; }); });
            });

    if (inputsTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ShaderInputsPanel>(inputsTabBar, m_context));
    }
    if (previewTabBar != nullptr) {
        m_panels.push_back(std::make_unique<ImagePreviewPanel>(previewTabBar, m_context, "Preview"));
    }
    if (sourceTabBar != nullptr) {
        s_addPlaceholderTab(sourceTabBar, "Source");
    }

    setupHotbar();

    if (Amethyst::LayoutConfig::instance().loadFromFile("layout.conf")) {
        if (auto *entry = Amethyst::LayoutConfig::instance().get("Texture Generator Dock")) {
            if (entry->type == Amethyst::ConfigType::DOCK_LAYOUT) {
                m_dockingLayer->applyConfig(entry->dockLayout);
            }
        }
    }
}

void TextureGeneratorWorkspace::saveLayout()
{
    Amethyst::LayoutConfig::instance().set(m_dockingLayer->name, Amethyst::ConfigEntry(m_dockingLayer->saveConfig()));
}

void TextureGeneratorWorkspace::setupHotbar()
{
    auto *layout = m_hotbar->addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
    layout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
    layout->innerPadding = Amethyst::UDim::fromOffset(6.0f);

    Amethyst::UIScope hotbarScope(*m_hotbar);

    static const Amethyst::TextStyleProperties s_btnText = {.textXAlignment = Amethyst::TextXAlignment::CENTER,
                                                             .textYAlignment = Amethyst::TextYAlignment::CENTER};

    hotbarScope
        .dropdown({.base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(200.0f, 28.0f)},
                   .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
                   .label = "No instances"},
                  [this](Amethyst::DropdownScope &d) { m_instanceDropdown = &d.component; })
        .textButton({.base = {.layoutOrder = 1, .size = Amethyst::UDim2::fromOffset(50.0f, 28.0f)},
                     .text = s_btnText,
                     .label = "New"},
                    [this](Amethyst::TextButtonScope &b) {
                        m_newBtn = &b.component;
                        m_newBtn->onMouseButton1ClickCb = [this]() {
                            if (m_newPopup != nullptr) {
                                refreshShaderDropdown();
                                m_newPopup->open(m_newBtn);
                            }
                            return Amethyst::EventResult::CONSUMED;
                        };
                    })
        .frame({.base = {.layoutOrder = 2, .size = Amethyst::UDim2::fromOffset(1.0f, 24.0f)},
                .style = {.backgroundColor = Amethyst::Color4::fromHex(0x444444)}})
        .textButton({.base = {.layoutOrder = 3, .size = Amethyst::UDim2::fromOffset(80.0f, 28.0f)},
                     .text = s_btnText,
                     .label = "Generate"},
                    [this](Amethyst::TextButtonScope &b) {
                        b.component.onMouseButton1ClickCb = [this]() {
                            generate();
                            return Amethyst::EventResult::CONSUMED;
                        };
                    })
        .frame({.base = {.layoutOrder = 4, .size = Amethyst::UDim2::fromOffset(184.0f, 28.0f)},
                .style = {.backgroundTransparency = 1.0f}},
               [this](Amethyst::FrameScope &f) {
                   auto *grpLayout = f.component.addExtension<Amethyst::UIListLayout>();
                   grpLayout->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
                   grpLayout->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
                   grpLayout->innerPadding = Amethyst::UDim::fromOffset(6.0f);
                   f.checkbox({.base = {.layoutOrder = 0, .size = Amethyst::UDim2::fromOffset(18.0f, 18.0f)},
                               .value = &m_autoGenerate});
                   f.textLabel({.base = {.layoutOrder = 1, .size = Amethyst::UDim2::fromOffset(160.0f, 28.0f)},
                                .label = "Run generate on change"});
               });

    Amethyst::UIScope(*m_container)
        .popup({.base = {.size = Amethyst::UDim2::fromOffset(380.0f, 188.0f)},
                .placement = Amethyst::PopupPlacement::BELOW,
                .closeOnClickOutside = false},
               [this](Amethyst::PopupScope &p) {
                   m_newPopup = &p.component;
                   auto *vLayout = m_newPopup->addExtension<Amethyst::UIListLayout>();
                   vLayout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
                   vLayout->innerPadding = Amethyst::UDim::fromOffset(0.0f);

                   p.frame({.base = {.layoutOrder = 0, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 36.0f)},
                            .style = {.backgroundTransparency = 1.0f}},
                           [this](Amethyst::FrameScope &row) {
                               row.textLabel({.base = {.position = Amethyst::UDim2::fromOffset(8.0f, 0.0f),
                                                       .size = Amethyst::UDim2(0.0f, 60.0f, 1.0f, 0.0f)},
                                              .label = "Name"});
                               row.textInput({.base = {.position = Amethyst::UDim2(0.0f, 72.0f, 0.0f, 4.0f),
                                                       .size = Amethyst::UDim2(1.0f, -80.0f, 1.0f, -8.0f)},
                                              .textInput = {.text = {.textXAlignment = Amethyst::TextXAlignment::LEFT,
                                                                     .textYAlignment = Amethyst::TextYAlignment::CENTER}},
                                              .placeholder = "Instance name"},
                                             [this](Amethyst::TextInputScope &ti) { m_nameInput = &ti.component; });
                           });

                   p.frame({.base = {.layoutOrder = 1, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 36.0f)},
                            .style = {.backgroundTransparency = 1.0f}},
                           [this](Amethyst::FrameScope &row) {
                               row.textLabel({.base = {.position = Amethyst::UDim2::fromOffset(8.0f, 0.0f),
                                                       .size = Amethyst::UDim2(0.0f, 60.0f, 1.0f, 0.0f)},
                                              .label = "Width"});
                               row.numberInput({.base = {.position = Amethyst::UDim2(0.0f, 72.0f, 0.0f, 4.0f),
                                                         .size = Amethyst::UDim2(1.0f, -80.0f, 1.0f, -8.0f)},
                                                .textInput = {.text = {.textXAlignment = Amethyst::TextXAlignment::LEFT,
                                                                       .textYAlignment = Amethyst::TextYAlignment::CENTER}},
                                                .placeholder = "1024",
                                                .allowDecimal = false,
                                                .allowNegative = false},
                                               [this](Amethyst::NumberInputScope &ni) {
                                                   m_widthInput = &ni.component;
                                                   m_widthInput->setText("1024");
                                               });
                           });

                   p.frame({.base = {.layoutOrder = 2, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 36.0f)},
                            .style = {.backgroundTransparency = 1.0f}},
                           [this](Amethyst::FrameScope &row) {
                               row.textLabel({.base = {.position = Amethyst::UDim2::fromOffset(8.0f, 0.0f),
                                                       .size = Amethyst::UDim2(0.0f, 60.0f, 1.0f, 0.0f)},
                                              .label = "Height"});
                               row.numberInput({.base = {.position = Amethyst::UDim2(0.0f, 72.0f, 0.0f, 4.0f),
                                                         .size = Amethyst::UDim2(1.0f, -80.0f, 1.0f, -8.0f)},
                                                .textInput = {.text = {.textXAlignment = Amethyst::TextXAlignment::LEFT,
                                                                       .textYAlignment = Amethyst::TextYAlignment::CENTER}},
                                                .placeholder = "1024",
                                                .allowDecimal = false,
                                                .allowNegative = false},
                                               [this](Amethyst::NumberInputScope &ni) {
                                                   m_heightInput = &ni.component;
                                                   m_heightInput->setText("1024");
                                               });
                           });

                   p.frame({.base = {.layoutOrder = 3, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 36.0f)},
                            .style = {.backgroundTransparency = 1.0f}},
                           [this](Amethyst::FrameScope &row) {
                               row.textLabel({.base = {.position = Amethyst::UDim2::fromOffset(8.0f, 0.0f),
                                                       .size = Amethyst::UDim2(0.0f, 60.0f, 1.0f, 0.0f)},
                                              .label = "Shader"});
                               row.dropdown({.base = {.position = Amethyst::UDim2(0.0f, 72.0f, 0.0f, 4.0f),
                                                      .size = Amethyst::UDim2(1.0f, -80.0f, 1.0f, -8.0f)},
                                             .label = "Select shader..."},
                                            [this](Amethyst::DropdownScope &d) { m_shaderDropdown = &d.component; });
                           });

                   p.frame({.base = {.layoutOrder = 4, .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 44.0f)},
                            .style = {.backgroundTransparency = 1.0f}},
                           [this](Amethyst::FrameScope &row) {
                               row.textButton({.base = {.anchorPoint = Amethyst::vec2(0.0f, 0.5f),
                                                        .position = Amethyst::UDim2(0.0f, 8.0f, 0.5f, 0.0f),
                                                        .size = Amethyst::UDim2(0.5f, -12.0f, 0.0f, 28.0f)},
                                               .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER,
                                                        .textYAlignment = Amethyst::TextYAlignment::CENTER},
                                               .label = "Cancel"},
                                              [this](Amethyst::TextButtonScope &b) {
                                                  b.component.onMouseButton1ClickCb = [this]() {
                                                      if (m_newPopup != nullptr) m_newPopup->close();
                                                      return Amethyst::EventResult::CONSUMED;
                                                  };
                                              });
                               row.textButton({.base = {.anchorPoint = Amethyst::vec2(1.0f, 0.5f),
                                                        .position = Amethyst::UDim2(1.0f, -8.0f, 0.5f, 0.0f),
                                                        .size = Amethyst::UDim2(0.5f, -12.0f, 0.0f, 28.0f)},
                                               .text = {.textXAlignment = Amethyst::TextXAlignment::CENTER,
                                                        .textYAlignment = Amethyst::TextYAlignment::CENTER},
                                               .label = "Create"},
                                              [this](Amethyst::TextButtonScope &b) {
                                                  b.component.onMouseButton1ClickCb = [this]() {
                                                      if (m_nameInput == nullptr || m_widthInput == nullptr ||
                                                          m_heightInput == nullptr ||
                                                          m_pendingShaderHandle == Rapture::AssetHandle{}) {
                                                          return Amethyst::EventResult::CONSUMED;
                                                      }
                                                      std::string name = m_nameInput->getText();
                                                      if (name.empty()) name = "Instance";
                                                      int64_t w = m_widthInput->asInt64();
                                                      int64_t h = m_heightInput->asInt64();
                                                      uint32_t width = static_cast<uint32_t>(w > 0 ? w : 1024);
                                                      uint32_t height = static_cast<uint32_t>(h > 0 ? h : 1024);
                                                      createInstance(name, width, height, m_pendingShaderHandle);
                                                      m_newPopup->close();
                                                      return Amethyst::EventResult::CONSUMED;
                                                  };
                                              });
                           });
               });
}

void TextureGeneratorWorkspace::rebuildInstanceDropdown()
{
    if (m_instanceDropdown == nullptr) return;

    std::vector<Amethyst::ContextMenuItem> items;
    items.reserve(m_instances.size());
    for (size_t i = 0; i < m_instances.size(); ++i) {
        items.push_back(Amethyst::ContextMenuItem::action(m_instances[i]->name, [this, i]() { selectInstance(i); }));
    }
    m_instanceDropdown->setItems(std::move(items));

    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_instances.size())) {
        m_instanceDropdown->setText(m_instances[m_selectedIndex]->name);
    } else {
        m_instanceDropdown->setText("No instances");
    }
}

void TextureGeneratorWorkspace::refreshShaderDropdown()
{
    if (m_shaderDropdown == nullptr) return;

    const auto &registry = Rapture::AssetManager::getAssetRegistry();
    std::vector<Amethyst::ContextMenuItem> items;

    for (const auto &[handle, metaPtr] : registry) {
        if (!metaPtr || metaPtr->assetType != Rapture::AssetType::SHADER) continue;

        auto assetRef = Rapture::AssetManager::getAsset(handle);
        if (!assetRef) continue;
        auto *shader = assetRef.get()->getUnderlyingAsset<Rapture::Shader>();
        if (shader == nullptr || !shader->isReady()) continue;
        if (!shader->hasStage(Rapture::ShaderType::COMPUTE)) continue;

        std::string name = metaPtr->getName();
        Rapture::AssetHandle h = handle;
        items.push_back(Amethyst::ContextMenuItem::action(name, [this, h, name]() {
            m_pendingShaderHandle = h;
            if (m_shaderDropdown != nullptr) m_shaderDropdown->setText(name);
        }));
    }

    m_shaderDropdown->setItems(std::move(items));
    m_pendingShaderHandle = Rapture::AssetHandle{};
    m_shaderDropdown->setText("Select shader...");
}

void TextureGeneratorWorkspace::selectInstance(size_t index)
{
    m_selectedIndex = static_cast<int>(index);
    rebuildInstanceDropdown();

    TextureGeneratorInstance *instance = (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_instances.size()))
                                             ? m_instances[m_selectedIndex].get()
                                             : nullptr;

    for (auto &panel : m_panels) {
        if (auto *inp = dynamic_cast<ShaderInputsPanel *>(panel.get())) {
            if (instance != nullptr) {
                inp->setInstance(instance, [this]() {
                    if (m_autoGenerate) generate();
                });
            } else {
                inp->clearInstance();
            }
        }
        if (auto *prev = dynamic_cast<ImagePreviewPanel *>(panel.get())) {
            if (instance != nullptr && instance->previewAmTexId.isValid()) {
                prev->setImage(instance->previewAmTexId);
            } else {
                prev->clearImage();
            }
        }
    }
}

void TextureGeneratorWorkspace::createInstance(const std::string &name, uint32_t width, uint32_t height,
                                               Rapture::AssetHandle shaderHandle)
{
    auto instance = std::make_unique<TextureGeneratorInstance>();
    instance->name = name;
    instance->width = width;
    instance->height = height;
    instance->shaderHandle = shaderHandle;

    Rapture::ProceduralTextureConfig config;
    config.name = name;
    instance->generator = std::make_unique<Rapture::ProceduralTexture>(shaderHandle, config);

    if (!instance->generator->isValid()) {
        RP_ERROR("Failed to create procedural texture generator for '{}'", name);
        return;
    }

    size_t pcSize = instance->generator->getExpectedPushConstantSize();
    if (pcSize > 0) {
        instance->buffer.resize(pcSize, 0);
        ShaderInputsPanel::applyShaderDefaults(instance->buffer, instance->generator->getShader());
    }

    m_instances.push_back(std::move(instance));
    selectInstance(m_instances.size() - 1);
}

void TextureGeneratorWorkspace::generate()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_instances.size())) {
        RP_WARN("Generate called with no selected instance");
        return;
    }

    TextureGeneratorInstance *instance = m_instances[m_selectedIndex].get();
    if (instance->generator == nullptr || !instance->generator->isValid()) {
        RP_ERROR("Generate called but instance '{}' has no valid generator", instance->name);
        return;
    }

    if (!instance->buffer.empty()) {
        instance->generator->setPushConstantsRaw(instance->buffer.data(), instance->buffer.size());
    }

    instance->generator->generate();
    RP_INFO("Generated texture for instance '{}'", instance->name);

    if (!instance->previewAmTexId.isValid()) {
        if (!m_context.services.registerTexture) {
            RP_ERROR("registerTexture service not set, cannot display preview");
        } else {
            Rapture::Texture &tex = instance->generator->getTexture();
            instance->previewAmTexId = m_context.services.registerTexture(&tex);
            RP_INFO("Registered texture, id={}", instance->previewAmTexId.id);
        }
    }

    if (instance->previewAmTexId.isValid()) {
        bool found = false;
        for (auto &panel : m_panels) {
            if (auto *prev = dynamic_cast<ImagePreviewPanel *>(panel.get())) {
                prev->setImage(instance->previewAmTexId);
                found = true;
            }
        }
        if (!found) {
            RP_WARN("No ImagePreviewPanel found to display generated texture");
        }
    } else {
        RP_ERROR("Texture registration failed, previewAmTexId is invalid");
    }
}
