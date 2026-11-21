#include "PropertiesPanel.h"
#include "EntitySelection.h"
#include "Icons.h"
#include "inspector/ComponentEditors.h"
#include "ui/controls/IconLabelButton.h"
#include "ui/controls/SearchField.h"

#include <rapture/scene/World.h>
#include <rapture/scene/components/Components.h>
#include <rapture/scene/instances/PrimitiveMesh3D.h>
#include <rapture/scene/instances/SceneObject.h>

#include <components/common.h>
#include <components/extensions/ui_flex_item.h>
#include <components/extensions/ui_list_layout.h>
#include <components/ui_scope.h>
#include <modules/color.h>

#include <string>
#include <vector>

static constexpr float SEARCH_BAR_HEIGHT = 26.0f;
static constexpr float SEARCH_BAR_TOP_PAD = 8.0f;
static constexpr float SEARCH_BAR_SIDE_PAD = 8.0f;
static constexpr float SEARCH_BAR_GAP = 6.0f;
static constexpr std::string_view ADD_COMPONENT_BUTTON_CLASS = "generic-text-button";
static constexpr float CONTENT_OFFSET = SEARCH_BAR_TOP_PAD + SEARCH_BAR_HEIGHT + 8.0f;

PropertiesPanel::PropertiesPanel(const WorkspaceContext &context) : Panel("Properties", context)
{
    m_icon = Icons::SVG_PROPERTIES;
}

void PropertiesPanel::build(Amethyst::UIScope &scope)
{
    setupSearchBar(scope);
    setupPlaceholder(scope);
    setupEntityView();

    m_addComponentWindow.emplace(*m_root);
    m_addComponentWindow->onComponentAdded = [this]() { refresh(); };

    if (m_selection != nullptr) {
        m_selectionChangedConn = m_selection->onChanged.connect([this](Rapture::ecs::EntityAccessor entity) {
            if (entity.isValid()) {
                showEntity(entity);
            } else {
                clearSelection();
            }
        });
    }

    setScene(m_context.scene);
}

void PropertiesPanel::onRootDestroyed()
{
    m_placeholderText = nullptr;
    m_searchField = nullptr;
    m_addComponentButton = nullptr;
}

void PropertiesPanel::setupSearchBar(Amethyst::UIScope &scope)
{
    scope.frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(SEARCH_BAR_SIDE_PAD, SEARCH_BAR_TOP_PAD),
                    .size = Amethyst::UDim2(1.0f, -2.0f * SEARCH_BAR_SIDE_PAD, 0.0f, SEARCH_BAR_HEIGHT),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &bar) {
            auto *row = bar.component.addExtension<Amethyst::UIListLayout>();
            row->fillDirection = Amethyst::FillDirection::FILL_HORIZONTAL;
            row->horizontalFlex = Amethyst::UiFlexAlignment::FILL;
            row->verticalAlignment = Amethyst::VerticalAlignment::ALIGN_CENTER_V;
            row->innerPadding = Amethyst::UDim::fromOffset(SEARCH_BAR_GAP);

            m_searchField = bar.component.add<SearchField>("Search properties");
            m_searchField->setBaseProperties({.layoutOrder = 0, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)});
            m_searchField->addExtension<Amethyst::UIFlexItem>();

            m_addComponentButton = bar.component.add<IconLabelButton>(Icons::SVG_PLUS, "Add Component",
                                                                      std::vector<std::string>{std::string(ADD_COMPONENT_BUTTON_CLASS)});
            m_addComponentButton->setBaseProperties({.layoutOrder = 1, .size = Amethyst::UDim2::fromScale(0.0f, 1.0f)});
            m_addComponentButton->track(m_addComponentButton->onMouseButton1ClickCb.connect([this]() { openAddComponentWindow(); }));
        });
}

void PropertiesPanel::openAddComponentWindow()
{
    Rapture::SceneObject *object = m_scene != nullptr && m_selectedEntity.isValid() ? m_scene->instanceFor(m_selectedEntity.getEntity()) : nullptr;
    if (object == nullptr || !m_addComponentWindow.has_value()) {
        return;
    }

    m_addComponentWindow->open(*object);
}

void PropertiesPanel::setupPlaceholder(Amethyst::UIScope &scope)
{
    scope.textLabel(
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
        ensure<StaticMesh3DEditor>(instance->isA<Rapture::StaticMesh3D>() && !instance->isA<Rapture::PrimitiveMesh3D>());
        ensure<SkeletalMesh3DEditor>(instance->isA<Rapture::SkeletalMesh3D>());
        ensure<PhysicsEditor>(mesh != nullptr || instance->component<Rapture::PhysicsBody3D>() != nullptr);
        ensure<Light3DEditor>(instance->isA<Rapture::Light3D>());
        ensure<DirectionalLight3DEditor>(instance->isA<Rapture::DirectionalLight3D>());
        ensure<PointLight3DEditor>(instance->isA<Rapture::PointLight3D>());
        ensure<SpotLight3DEditor>(instance->isA<Rapture::SpotLight3D>());
        ensure<SpringArm3DEditor>(instance->isA<Rapture::SpringArm3D>());
        ensure<Camera3DEditor>(instance->isA<Rapture::Camera3D>());

        ensure<CameraControllerEditor>(instance->isA<Rapture::CameraController>());
        ensure<PlayerControllerEditor>(instance->isA<Rapture::PlayerController>());

        ensure<EnvironmentEditor>(instance->isA<Rapture::Environment>());

        ensure<ShadowEditor>(e.has<Rapture::ShadowComponent>());
        ensure<CascadedShadowEditor>(e.has<Rapture::CascadedShadowComponent>());

        ensure<VisibilityEditor>(true);
        ensure<AttributesEditor>(true);
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
