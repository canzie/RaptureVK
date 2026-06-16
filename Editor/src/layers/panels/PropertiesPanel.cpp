#include "PropertiesPanel.h"
#include "Icons.h"
#include "components/Components.h"
#include "components/TerrainComponent.h"
#include "events/GameEvents.h"

#include <components/common.h>
#include <components/ui_scope.h>

static constexpr float k_searchBarHeight = 28.0f;
static constexpr float k_searchBarTopPad = 8.0f;
static constexpr float k_contentOffset = k_searchBarTopPad + k_searchBarHeight + 8.0f;

static constexpr float k_headerHeight = 28.0f;
static constexpr float k_sectionSpacing = 6.0f;
static constexpr float k_sectionTopPad = 4.0f;
static constexpr float k_stubBodyHeight = 36.0f;
static constexpr float k_transformBodyHeight = 96.0f;

PropertiesPanel::PropertiesPanel(Amethyst::TabBar *tabBar) : m_hostTabBar(tabBar)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_root->name = "Properties";
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

    setupSearchBar();
    setupPlaceholder();
    setupEntityView();

    m_hostTabBar->addTab(std::move(root), "Properties");

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
    if (m_hostTabBar != nullptr && m_root != nullptr) {
        m_hostTabBar->removeTab(m_root);
    }
}

void PropertiesPanel::setupSearchBar()
{
    Amethyst::UIScope(*m_root).frame(
        {
            .base =
                {
                    .position = Amethyst::UDim2(0.02f, 0.0f, 0.0f, k_searchBarTopPad),
                    .size = Amethyst::UDim2(0.96f, 0.0f, 0.0f, k_searchBarHeight),
                },
            .style = {.backgroundTransparency = 1.0f},
        },
        [this](Amethyst::FrameScope &bar) {
            bar.frame(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2::fromOffset(0.0f, 0.0f),
                            .size = Amethyst::UDim2(0.82f, -3.0f, 1.0f, 0.0f),
                        },
                    .style =
                        {
                            .backgroundColor = Amethyst::Color3::fromHex(0x181818),
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
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, k_contentOffset),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -k_contentOffset),
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
            .base =
                {
                    .clipsDescendants = true,
                    .position = Amethyst::UDim2(0.0f, 0.0f, 0.0f, k_contentOffset),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 1.0f, -k_contentOffset),
                    .visible = false,
                },
            .scroll =
                {
                    .scrollAxis = Amethyst::ScrollAxis::Y,
                    .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                    .canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
                },
        },
        [this](Amethyst::ScrollingFrameScope &sf) {
            m_entityView = &sf.component;
            setupTransformHeader(sf);
            setupMeshHeader(sf);
            setupMaterialHeader(sf);
            setupLightHeader(sf);
            setupCameraHeader(sf);
            setupShadowHeader(sf);
            setupCascadedShadowHeader(sf);
            setupSkyboxHeader(sf);
            setupTerrainHeader(sf);
        });
}

Amethyst::CollapsibleHeader *PropertiesPanel::beginSection(Amethyst::ScrollingFrameScope &sf, const char *title, float bodyHeight,
                                                           std::function<bool(const Rapture::Entity &)> matches,
                                                           std::function<void(Amethyst::CollapsibleHeaderScope &)> body)
{
    Amethyst::CollapsibleHeader *header = nullptr;
    sf.collapsibleHeader(
        {
            .base =
                {
                    .position = Amethyst::UDim2::fromOffset(0.0f, k_sectionTopPad),
                    .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, k_headerHeight + bodyHeight),
                    .visible = false,
                },
            .header =
                {
                    .titleStyle = {.fontSize = 13.0f},
                    .headerHeight = k_headerHeight,
                },
            .title = title,
        },
        [&](Amethyst::CollapsibleHeaderScope &ch) {
            header = &ch.component;
            if (body) {
                body(ch);
            }
        });

    header->onToggled = [this](bool) { relayout(); };
    m_sections.push_back({header, bodyHeight, std::move(matches)});
    return header;
}

void PropertiesPanel::addStubBody(Amethyst::CollapsibleHeaderScope &ch)
{
    ch.textLabel({
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(4.0f, 4.0f),
                .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, k_stubBodyHeight),
            },
        .style = {.backgroundTransparency = 1.0f},
        .text =
            {
                .fontSize = 12.0f,
                .textColor = Amethyst::Color4(0.5f, 0.5f, 0.5f, 1.0f),
                .textXAlignment = Amethyst::TextXAlignment::LEFT,
                .textYAlignment = Amethyst::TextYAlignment::CENTER,
            },
        .label = "Not yet implemented",
    });
}

void PropertiesPanel::setupTransformHeader(Amethyst::ScrollingFrameScope &sf)
{
    struct SliderConfig {
        const char *label;
        glm::vec3 min;
        glm::vec3 max;
        float speed;
    };
    static const SliderConfig configs[3] = {
        {"Translation", glm::vec3(-1000.0f), glm::vec3(1000.0f), 0.1f},
        {"Rotation", glm::vec3(-360.0f), glm::vec3(360.0f), 0.5f},
        {"Scale", glm::vec3(-100.0f), glm::vec3(100.0f), 0.01f},
    };

    m_transformHeader = beginSection(
        sf, "Transform", k_transformBodyHeight,
        [](const Rapture::Entity &e) { return e.hasComponent<Rapture::TransformComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) {
            ch.table(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2::fromOffset(4.0f, 4.0f),
                            .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 88.0f),
                        },
                    .table =
                        {
                            .rowHeight = 28.0f,
                            .cellPadding =
                                {
                                    Amethyst::UDim::fromOffset(0.0f),
                                    Amethyst::UDim::fromOffset(4.0f),
                                    Amethyst::UDim::fromOffset(0.0f),
                                    Amethyst::UDim::fromOffset(4.0f),
                                },
                            .showColumnSeparators = false,
                            .showHeader = false,
                        },
                },
                [this](Amethyst::TableScope &t) {
                    m_transformTable = &t.component;
                    t.column("", 0.28f, Amethyst::TableColumnSizing::FIXED);
                    t.column("", 0.72f);

                    for (int row = 0; row < 3; ++row) {
                        t.row([this, row](Amethyst::TableRowScope &tr) {
                            tr.cell([row](Amethyst::UIScope &cell) {
                                cell.textLabel({
                                    .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                                    .style = {.backgroundTransparency = 1.0f},
                                    .text =
                                        {
                                            .fontSize = 12.0f,
                                            .textColor = Amethyst::Color4(0.7f, 0.7f, 0.7f, 1.0f),
                                            .textXAlignment = Amethyst::TextXAlignment::CENTER,
                                            .textYAlignment = Amethyst::TextYAlignment::CENTER,
                                        },
                                    .label = configs[row].label,
                                });
                            });
                            tr.cell([this, row](Amethyst::UIScope &cell) {
                                for (int axis = 0; axis < 3; ++axis) {
                                    cell.dragFloat(
                                        {
                                            .base =
                                                {
                                                    .position = Amethyst::UDim2(axis / 3.0f, axis == 0 ? 0.0f : 2.0f, 0.0f, 0.0f),
                                                    .size = Amethyst::UDim2(1.0f / 3.0f, -2.0f, 1.0f, 0.0f),
                                                },
                                            .speed = configs[row].speed,
                                            .min = configs[row].min[axis],
                                            .max = configs[row].max[axis],
                                            .value = &m_transformValues[row][axis],
                                        },
                                        [this, row, axis](Amethyst::DragFloatScope &d) {
                                            m_transformDrags[row][axis] = &d.component;
                                            d.component.onValueChanged = [this, row](double) {
                                                if (!m_selectedEntity.isValid()) {
                                                    return;
                                                }
                                                if (!m_selectedEntity.hasComponent<Rapture::TransformComponent>()) {
                                                    return;
                                                }
                                                auto &tc = m_selectedEntity.getComponent<Rapture::TransformComponent>();
                                                glm::vec3 v(static_cast<float>(m_transformValues[row][0]),
                                                            static_cast<float>(m_transformValues[row][1]),
                                                            static_cast<float>(m_transformValues[row][2]));
                                                if (row == 0) {
                                                    tc.transforms.setTranslation(v);
                                                } else if (row == 1) {
                                                    tc.transforms.setRotation(v);
                                                } else {
                                                    tc.transforms.setScale(v);
                                                }
                                                m_selectedEntity.markDirty();
                                            };
                                        });
                                }
                            });
                        });
                    }
                });
        });
}

void PropertiesPanel::setupMeshHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Mesh", k_stubBodyHeight + 8.0f, [](const Rapture::Entity &e) { return e.hasComponent<Rapture::MeshComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::setupMaterialHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Material", k_stubBodyHeight + 8.0f,
        [](const Rapture::Entity &e) { return e.hasComponent<Rapture::MaterialComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::setupLightHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Light", k_stubBodyHeight + 8.0f, [](const Rapture::Entity &e) { return e.hasComponent<Rapture::LightComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::setupCameraHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Camera", k_stubBodyHeight + 8.0f, [](const Rapture::Entity &e) { return e.hasComponent<Rapture::CameraComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::setupShadowHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Shadow", k_stubBodyHeight + 8.0f, [](const Rapture::Entity &e) { return e.hasComponent<Rapture::ShadowComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::setupCascadedShadowHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Cascaded Shadow", k_stubBodyHeight + 8.0f,
        [](const Rapture::Entity &e) { return e.hasComponent<Rapture::CascadedShadowComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::setupSkyboxHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Skybox", k_stubBodyHeight + 8.0f, [](const Rapture::Entity &e) { return e.hasComponent<Rapture::SkyboxComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::setupTerrainHeader(Amethyst::ScrollingFrameScope &sf)
{
    beginSection(
        sf, "Terrain", k_stubBodyHeight + 8.0f,
        [](const Rapture::Entity &e) { return e.hasComponent<Rapture::TerrainComponent>(); },
        [this](Amethyst::CollapsibleHeaderScope &ch) { addStubBody(ch); });
}

void PropertiesPanel::relayout()
{
    if (m_entityView == nullptr) {
        return;
    }

    float y = k_sectionTopPad;
    for (auto &section : m_sections) {
        bool visible = m_selectedEntity.isValid() && section.matches(m_selectedEntity);
        section.header->setBaseProperties({
            .position = Amethyst::UDim2::fromOffset(0.0f, y),
            .visible = visible,
        });
        if (visible) {
            bool expanded = static_cast<bool>(section.header->getCollapsibleHeaderProperties().expanded);
            y += k_headerHeight + (expanded ? section.bodyHeight : 0.0f) + k_sectionSpacing;
        }
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
    if (!m_selectedEntity.isValid()) {
        return;
    }
    if (!m_selectedEntity.hasComponent<Rapture::TransformComponent>()) {
        return;
    }

    const auto &transform = m_selectedEntity.getComponent<Rapture::TransformComponent>();
    auto t = transform.translation();
    auto r = transform.rotation();
    auto sc = transform.scale();
    m_transformValues[0][0] = t.x;
    m_transformValues[0][1] = t.y;
    m_transformValues[0][2] = t.z;
    m_transformValues[1][0] = r.x;
    m_transformValues[1][1] = r.y;
    m_transformValues[1][2] = r.z;
    m_transformValues[2][0] = sc.x;
    m_transformValues[2][1] = sc.y;
    m_transformValues[2][2] = sc.z;
}

void PropertiesPanel::showEntity(const Rapture::Entity &entity)
{
    m_selectedEntity = entity;
    m_placeholderText->setBaseProperties({.visible = false});
    m_entityView->setBaseProperties({.visible = true});
    relayout();
}

void PropertiesPanel::showPlaceholder()
{
    m_placeholderText->setBaseProperties({.visible = true});
    m_entityView->setBaseProperties({.visible = false});
}
