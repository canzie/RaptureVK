#include "PropertiesPanel.h"
#include "EntitySelection.h"
#include "Icons.h"
#include "layers/panels/component_editors/ComponentEditors.h"
#include "layers/panels/components/tab_layouts.h"

#include "components/Components.h"
#include "scenes/World.h"
#include "scenes/instances/SceneObject.h"

#include <components/common.h>
#include <components/ui_scope.h>
#include <modules/color.h>

static constexpr float SEARCH_BAR_HEIGHT = 28.0f;
static constexpr float SEARCH_BAR_TOP_PAD = 8.0f;
static constexpr float CONTENT_OFFSET = SEARCH_BAR_TOP_PAD + SEARCH_BAR_HEIGHT + 8.0f;

PropertiesPanel::PropertiesPanel(Amethyst::TabBar *tabBar, const WorkspaceContext &context) : Panel("Properties", context)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_rootDestroyConn = m_root->onDestroy.connect([this](Amethyst::Instance *) {
        m_root = nullptr;
        m_placeholderText = nullptr;
        m_searchInput = nullptr;
    });
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    setupSearchBar();
    setupPlaceholder();
    setupEntityView();

    icon = Icons::SVG_PROPERTIES;
    attach(tabBar, std::move(root));

    if (m_selection != nullptr) {
        m_selectionChangedConn = m_selection->onChanged.connect([this](Rapture::ecs::EntityAccessor entity) {
            if (entity.isValid()) {
                showEntity(entity);
            } else {
                clearSelection();
            }
        });
    }

    setScene(context.scene);
}

PropertiesPanel::~PropertiesPanel()
{
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
                    .classes = {"property-input-field"},
                    .base =
                        {
                            .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                            .size = Amethyst::UDim2(0.82f, -3.0f, 1.0f, 0.0f),
                        },
                },
                [this](Amethyst::FrameScope &sf) {
                    auto *searchFrame = &sf.component;
                    sf.track(sf.component.onHoverChanged.connect([searchFrame](bool hovered) {
                        searchFrame->setBaseStyleProperties({.borderPixelSize = hovered ? 1.0f : 0.0f});
                    }));
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
                            .textInput = {.text =
                                              {
                                                  .textColor = Amethyst::Color4::fromHex(0xEEEEEE),
                                                  .textYAlignment = Amethyst::TextYAlignment::CENTER,
                                              },
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
                    .style = {.backgroundTransparency = 1.0f},
                    .image = {.imageColor = Amethyst::Color4(0.6f, 0.6f, 0.6f, 1.0f)},
                    .svg = Icons::SVG_FILTER,
                },
                [](Amethyst::ImageButtonScope &btn) {
                    btn.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
                    auto *b = &btn.component;
                    b->track(b->onHoverChanged.connect([b](bool hovered) {
                        b->setImageStyleProperties({.imageColor = hovered ? Amethyst::Color4{0.85f, 0.85f, 0.85f, 1.0f}
                                                                          : Amethyst::Color4{0.6f, 0.6f, 0.6f, 1.0f}});
                    }));
                });
            bar.imageButton(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2(0.91f, 1.0f, 0.0f, 3.0f),
                            .size = Amethyst::UDim2(0.09f, 0.0f, 1.0f, -6.0f),
                        },
                    .style = {.backgroundTransparency = 1.0f},
                    .image = {.imageColor = Amethyst::Color4(0.6f, 0.6f, 0.6f, 1.0f)},
                    .svg = Icons::SVG_MORE,
                },
                [](Amethyst::ImageButtonScope &btn) {
                    btn.component.addExtension<Amethyst::UIAspectRatioConstraint>()->dominantAxis = Amethyst::DominantAxis::HEIGHT;
                    auto *b = &btn.component;
                    b->track(b->onHoverChanged.connect([b](bool hovered) {
                        b->setImageStyleProperties({.imageColor = hovered ? Amethyst::Color4{0.85f, 0.85f, 0.85f, 1.0f}
                                                                          : Amethyst::Color4{0.6f, 0.6f, 0.6f, 1.0f}});
                    }));
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
    m_sections.emplace(*m_root, Amethyst::ScrollingFrameProperties{
                                    .classes = {"panel"},
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
                                });
}

void PropertiesPanel::refresh()
{
    m_sections->refresh([this]() {
        if (!m_selectedEntity.isValid()) {
            return;
        }
        const Rapture::ecs::EntityAccessor &e = m_selectedEntity;

        // Sections come from the instance's class chain, base first, so a Transform sits above the
        // sections its subclasses add.
        Rapture::SceneObject *instance = m_scene != nullptr ? m_scene->instanceFor(e.getEntity()) : nullptr;
        if (instance == nullptr) {
            return;
        }

        Rapture::Mesh3D *mesh = instance->as<Rapture::Mesh3D>();

        ensure<Node3DEditor>(instance->isA<Rapture::Node3D>());
        ensure<Mesh3DEditor>(mesh != nullptr);
        ensure<RigidBody3DEditor>(instance->component<Rapture::RigidBody3D>() != nullptr);
        ensure<Light3DEditor>(instance->isA<Rapture::Light3D>());
        ensure<DirectionalLight3DEditor>(instance->isA<Rapture::DirectionalLight3D>());
        ensure<PointLight3DEditor>(instance->isA<Rapture::PointLight3D>());
        ensure<SpotLight3DEditor>(instance->isA<Rapture::SpotLight3D>());
        ensure<Camera3DEditor>(instance->isA<Rapture::Camera3D>());

        ensure<CameraControllerEditor>(instance->isA<Rapture::CameraController>());
        ensure<PlayerControllerEditor>(instance->isA<Rapture::PlayerController>());

        ensure<EnvironmentEditor>(instance->isA<Rapture::Environment>());

        ensure<ShadowEditor>(e.has<Rapture::ShadowComponent>());
        ensure<CascadedShadowEditor>(e.has<Rapture::CascadedShadowComponent>());
    });
}

void PropertiesPanel::setScene(Rapture::Scene *scene)
{
    m_scene = scene;
    if (m_scene == nullptr) {
        m_selectedEntity = Rapture::ecs::EntityAccessor{};
        showPlaceholder();
    }
}

void PropertiesPanel::onUpdate(float dt)
{
    (void)dt;
    if (!m_selectedEntity.isValid()) {
        if (!m_sections->empty()) {
            clearSelection();
        }
        return;
    }

    if (m_sections->consumeRefreshRequest()) {
        refresh();
    }

    m_sections->sync();
}

void PropertiesPanel::showEntity(const Rapture::ecs::EntityAccessor &entity)
{
    m_selectedEntity = entity;
    m_placeholderText->setBaseProperties({.visible = false});
    m_sections->setVisible(true);
    refresh();
}

void PropertiesPanel::showPlaceholder()
{
    if (m_placeholderText == nullptr) {
        return;
    }
    m_placeholderText->setBaseProperties({.visible = true});
    m_sections->setVisible(false);
}

void PropertiesPanel::clearSelection()
{
    m_selectedEntity = Rapture::ecs::EntityAccessor{};
    refresh();
    showPlaceholder();
}
