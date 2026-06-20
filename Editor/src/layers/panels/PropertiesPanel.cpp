#include "PropertiesPanel.h"
#include "Icons.h"
#include "events/GameEvents.h"
#include "layers/panels/component_editors/ComponentEditors.h"
#include "layers/panels/components/header_layouts.h"
#include "layers/panels/components/tab_layouts.h"

#include "components/Components.h"

#include <components/common.h>
#include <components/ui_scope.h>

static constexpr float SEARCH_BAR_HEIGHT = 28.0f;
static constexpr float SEARCH_BAR_TOP_PAD = 8.0f;
static constexpr float CONTENT_OFFSET = SEARCH_BAR_TOP_PAD + SEARCH_BAR_HEIGHT + 8.0f;

static constexpr float HEADER_HEIGHT = 28.0f;
static constexpr float SECTION_SPACING = 6.0f;
static constexpr float SECTION_TOP_PAD = 4.0f;

PropertiesPanel::PropertiesPanel(Amethyst::TabBar *tabBar)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) { m_root = nullptr; });
    m_root->name = "Properties";
    m_root->addClass("background-secondary");
    m_root->setBaseProperties({.clipsDescendants = true});

    setupSearchBar();
    setupPlaceholder();
    setupEntityView();

    tabBar->addTab(std::move(root), iconTabLayout("Properties", Icons::SVG_PROPERTIES));

    m_entitySelectedListenerID =
        Rapture::GameEvents::onEntitySelected().addListener([this](std::shared_ptr<Rapture::Entity> entity) {
            if (!entity->isValid()) {
                return;
            }
            if (m_scene != nullptr && entity->getScene() != m_scene.get()) {
                return;
            }
            showEntity(*entity);
        });
}

PropertiesPanel::~PropertiesPanel()
{
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerID);
    if (m_root != nullptr && m_root->parent != nullptr) {
        if (auto *tabBar = m_root->parent->as<Amethyst::TabBar>()) {
            tabBar->removeTab(m_root);
        }
    }
}

void PropertiesPanel::setupSearchBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2(0.02f, 0.0f, 0.0f, SEARCH_BAR_TOP_PAD),
                    .size = Amethyst::UDim2(0.96f, 0.0f, 0.0f, SEARCH_BAR_HEIGHT),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &bar) {
            bar.frame(
                {
                    .classes = {"generic-input-field"},
                    .base =
                        {
                            .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                            .size = Amethyst::UDim2(0.82f, -3.0f, 1.0f, 0.0f),
                        },
                    .style =
                        {
                            .borderColor = Amethyst::Color3(0.4f, 0.4f, 0.4f),
                            .cornerRadius = 4.0f,
                        },
                },
                [this](Amethyst::FrameScope &sf) {
                    auto *searchFrame = &sf.component;
                    sf.component.onHoverChanged = [searchFrame](bool hovered) {
                        searchFrame->setBaseStyleProperties({.borderPixelSize = hovered ? 2.0f : 0.0f});
                    };
                    sf.imageLabel({
                        .base =
                            {
                                .anchorPoint = glm::vec2(0.0f, 0.5f),
                                .position = Amethyst::UDim2(0.0f, 7.0f, 0.5f, 0.0f),
                                .size = Amethyst::UDim2::fromOffset(14.0f, 14.0f),
                            },
                        .style = {.backgroundTransparency = 1.0f},
                        .image = {.imageColor = Amethyst::Color4(0.5f, 0.5f, 0.5f, 1.0f)},
                        .svg = Icons::SVG_SEARCH,
                    });
                    sf.textInput(
                        {
                            .base =
                                {
                                    .position = Amethyst::UDim2::fromOffset(26.0f, 0.0f),
                                    .size = Amethyst::UDim2(1.0f, -28.0f, 1.0f, 0.0f),
                                },
                            .style = {.backgroundTransparency = 1.0f},
                            .textInput = {.text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
                                          .cursorColor = Amethyst::Color4(0.9f, 0.9f, 0.9f, 1.0f)},
                            .placeholder = "Search Properties...",
                        },
                        [this](Amethyst::TextInputScope &ti) { m_searchInput = &ti.component; });
                });
            bar.imageButton(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2(0.82f, 3.0f, 0.0f, 3.0f),
                            .size = Amethyst::UDim2(0.09f, -2.0f, 1.0f, -6.0f),
                        },
                    .style = {.cornerRadius = 4.0f},
                    .image = {.imageColor = Amethyst::Color4(0.6f, 0.6f, 0.6f, 1.0f)},
                    .svg = Icons::SVG_FILTER,
                },
                [](Amethyst::ImageButtonScope &btn) {
                    btn.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
                    auto *b = &btn.component;
                    btn.component.onHoverChanged = [b](bool hovered) {
                        b->setImageStyleProperties({.imageColor = hovered ? Amethyst::Color4{0.85f, 0.85f, 0.85f, 1.0f}
                                                                          : Amethyst::Color4{0.6f, 0.6f, 0.6f, 1.0f}});
                        b->setBaseStyleProperties({.backgroundTransparency = hovered ? 0.85f : 1.0f});
                    };
                });
            bar.imageButton(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2(0.91f, 1.0f, 0.0f, 3.0f),
                            .size = Amethyst::UDim2(0.09f, 0.0f, 1.0f, -6.0f),
                        },
                    .style = {.cornerRadius = 4.0f},
                    .image = {.imageColor = Amethyst::Color4(0.6f, 0.6f, 0.6f, 1.0f)},
                    .svg = Icons::SVG_MORE,
                },
                [](Amethyst::ImageButtonScope &btn) {
                    btn.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
                    auto *b = &btn.component;
                    btn.component.onHoverChanged = [b](bool hovered) {
                        b->setImageStyleProperties({.imageColor = hovered ? Amethyst::Color4{0.85f, 0.85f, 0.85f, 1.0f}
                                                                          : Amethyst::Color4{0.6f, 0.6f, 0.6f, 1.0f}});
                        b->setBaseStyleProperties({.backgroundTransparency = hovered ? 0.85f : 1.0f});
                    };
                });
        });
}

void PropertiesPanel::setupPlaceholder()
{
    Amethyst::UIScope(*m_root).textLabel(
        {
            .base =
                {
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, CONTENT_OFFSET),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -CONTENT_OFFSET),
                },
            .style = {.backgroundTransparency = 1.0f},
            .text =
                {
                    .textColor = Amethyst::Color4(0.5f, 0.5f, 0.5f, 1.0f),
                    .textXAlignment = Amethyst::TextXAlignment::CENTER,
                    .textYAlignment = Amethyst::TextYAlignment::CENTER,
                },
            .label = "Select an entity",
        },
        [this](Amethyst::TextLabelScope &tl) { m_placeholderText = &tl.component; });
}

void PropertiesPanel::setupEntityView()
{
    Amethyst::UIScope(*m_root).scrollingFrame(
        {
            .classes = {"background-secondary"},
            .base =
                {
                    .clipsDescendants = true,
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, CONTENT_OFFSET),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -CONTENT_OFFSET),
                    .visible = false,
                },
            .scroll =
                {
                    .scrollAxis = Amethyst::ScrollAxis::Y,
                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                    .canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
                },
        },
        [this](Amethyst::ScrollingFrameScope &sf) { m_entityView = &sf.component; });
}

void PropertiesPanel::buildSection(ComponentEditorBase &editor)
{
    Amethyst::UIScope(*m_entityView)
        .collapsibleHeader(
            {
                .classes = {"background-tertiary"},
                .base =
                    {
                        .position = Amethyst::UDim2::fromOffset(0.0f, SECTION_TOP_PAD),
                        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, HEADER_HEIGHT),
                    },
                .style = {.backgroundTransparency = 1.0f},
                .header =
                    {
                        .titleStyle = {.fontSize = 13.0f},
                        .headerHeight = HEADER_HEIGHT,
                    },
            },
            [&](Amethyst::CollapsibleHeaderScope &ch) {
                editor.header = &ch.component;
                ch.header(componentHeaderLayout(editor.title(), editor.icon()));
                editor.buildBody(ch);
                editor.header->setBaseProperties({.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, HEADER_HEIGHT + editor.bodyHeight())});
            });

    editor.header->onToggled = [this](bool) { relayout(); };
}

void PropertiesPanel::refresh()
{
    m_active.clear();

    if (m_selectedEntity.isValid()) {
        const Rapture::Entity &e = m_selectedEntity;
        ensure<TransformEditor>(e.hasComponent<Rapture::TransformComponent>());
        ensure<MeshEditor>(e.hasComponent<Rapture::MeshComponent>());
        ensure<MaterialEditor>(e.hasComponent<Rapture::MaterialComponent>());
        ensure<LightEditor>(e.hasComponent<Rapture::LightComponent>());
        ensure<CameraEditor>(e.hasComponent<Rapture::CameraComponent>());
        ensure<ShadowEditor>(e.hasComponent<Rapture::ShadowComponent>());
        ensure<CascadedShadowEditor>(e.hasComponent<Rapture::CascadedShadowComponent>());
        ensure<SkyboxEditor>(e.hasComponent<Rapture::SkyboxComponent>());
    }

    relayout();
}

void PropertiesPanel::relayout()
{
    if (m_entityView == nullptr) {
        return;
    }

    float y = SECTION_TOP_PAD;
    for (auto *editor : m_active) {
        editor->header->setBaseProperties({.position = Amethyst::UDim2::fromOffset(0.0f, y)});
        bool expanded = static_cast<bool>(editor->header->getCollapsibleHeaderProperties().expanded);
        y += HEADER_HEIGHT + (expanded ? editor->bodyHeight() : 0.0f) + SECTION_SPACING;
    }

    m_entityView->setScrollingFrameProperties({.canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, y))});
}

void PropertiesPanel::setScene(std::shared_ptr<Rapture::Scene> scene)
{
    m_scene = scene;
    if (m_scene == nullptr) {
        m_selectedEntity = Rapture::Entity{};
        showPlaceholder();
    }
}

void PropertiesPanel::onUpdate(float dt)
{
    (void)dt;
    if (!m_selectedEntity.isValid()) {
        return;
    }
    for (auto *editor : m_active) {
        editor->sync(m_selectedEntity);
    }
}

void PropertiesPanel::showEntity(const Rapture::Entity &entity)
{
    m_selectedEntity = entity;
    m_placeholderText->setBaseProperties({.visible = false});
    m_entityView->setBaseProperties({.visible = true});
    refresh();
}

void PropertiesPanel::showPlaceholder()
{
    m_placeholderText->setBaseProperties({.visible = true});
    m_entityView->setBaseProperties({.visible = false});
}
