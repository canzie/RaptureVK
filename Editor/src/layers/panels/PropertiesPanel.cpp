#include "PropertiesPanel.h"
#include "Icons.h"
#include "components/Components.h"
#include "events/GameEvents.h"

#include <components/ui_scope.h>

static constexpr float k_searchBarHeight = 28.0f;
static constexpr float k_searchBarTopPad = 8.0f;
static constexpr float k_contentOffset = k_searchBarTopPad + k_searchBarHeight + 8.0f;

PropertiesPanel::PropertiesPanel(Amethyst::TabBar *tabBar) : m_hostTabBar(tabBar)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_root->name = "Properties";

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
                            .textInput = {.cursorColor = Amethyst::Color4(0.9f, 0.9f, 0.9f, 1.0f)},
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
                    .canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 600.0f)),
                },
        },
        [this](Amethyst::ScrollingFrameScope &sf) {
            m_entityView = &sf.component;
            sf.collapsibleHeader(
                {
                    .base =
                        {
                            .position = Amethyst::UDim2::fromOffset(0.0f, 4.0f),
                            .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 124.0f),
                        },
                    .header =
                        {
                            .titleStyle = {.fontSize = 13.0f},
                            .headerHeight = 28.0f,
                        },
                    .title = "Transform",
                },
                [this](Amethyst::CollapsibleHeaderScope &ch) {
                    m_transformHeader = &ch.component;
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
                            t.column("", 72.0f, Amethyst::TableColumnSizing::FIXED);
                            t.column("", 1.0f);

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
                                        cell.sliderVec3(
                                            {
                                                .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                                                .label = "",
                                            },
                                            [this, row](Amethyst::SliderVec3Scope &s) {
                                                m_transformSliders[row] = &s.component;
                                                s.component.min = configs[row].min;
                                                s.component.max = configs[row].max;
                                                s.component.speed = configs[row].speed;
                                                s.component.valueRef = &m_transformValues[row];
                                                s.component.onValueChanged = [this, row](Amethyst::vec3 val) {
                                                    if (!m_selectedEntity.isValid()) {
                                                        return;
                                                    }
                                                    if (!m_selectedEntity.hasComponent<Rapture::TransformComponent>()) {
                                                        return;
                                                    }
                                                    auto &tc = m_selectedEntity.getComponent<Rapture::TransformComponent>();
                                                    glm::vec3 v(val.x, val.y, val.z);
                                                    if (row == 0) {
                                                        tc.transforms.setTranslation(v);
                                                    } else if (row == 1) {
                                                        tc.transforms.setRotation(v);
                                                    } else {
                                                        tc.transforms.setScale(v);
                                                    }
                                                };
                                            });
                                    });
                                });
                            }
                        });
                });
        });
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
    m_searchInput->update(dt);

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
    m_transformValues[0] = Amethyst::vec3(t.x, t.y, t.z);
    m_transformValues[1] = Amethyst::vec3(r.x, r.y, r.z);
    m_transformValues[2] = Amethyst::vec3(sc.x, sc.y, sc.z);
}

void PropertiesPanel::showEntity(const Rapture::Entity &entity)
{
    m_selectedEntity = entity;
    m_placeholderText->setBaseProperties({.visible = false});
    m_entityView->setBaseProperties({.visible = true});
}

void PropertiesPanel::showPlaceholder()
{
    m_placeholderText->setBaseProperties({.visible = true});
    m_entityView->setBaseProperties({.visible = false});
}
