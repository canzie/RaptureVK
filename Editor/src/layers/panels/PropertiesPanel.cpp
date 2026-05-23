#include "PropertiesPanel.h"
#include "components/Components.h"
#include "events/GameEvents.h"

PropertiesPanel::PropertiesPanel(Amethyst::DockingLayer *dockingLayer) : m_dockingLayer(dockingLayer)
{
    auto root = std::make_unique<Amethyst::PanelLayer>();
    m_root = root.get();
    m_root->name = "Properties";
    m_root->markDirty();

    setupPlaceholder();
    setupEntityView();

    m_dockingLayer->dock(std::move(root), glm::vec2(0.0f));

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
    if (m_dockingLayer && m_root) {
        m_dockingLayer->undock(m_root);
    }
}

void PropertiesPanel::setupPlaceholder()
{
    m_placeholderText = m_root->add<Amethyst::TextLabel>();
    m_placeholderText->text = "Select an entity";
    m_placeholderText->textColor = Amethyst::Color4(0.5f, 0.5f, 0.5f, 1.0f);
    m_placeholderText->size = Amethyst::UDim2::fromScale(1.0f);
    m_placeholderText->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_placeholderText->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_placeholderText->backgroundTransparency = 1.0f;
    m_placeholderText->markDirty();
}

void PropertiesPanel::setupEntityView()
{
    m_entityView = m_root->add<Amethyst::ScrollingFrame>();
    m_entityView->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
    m_entityView->clipsDescendants = true;
    m_entityView->scrollAxis = Amethyst::ScrollAxis::Y;
    m_entityView->scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO;
    m_entityView->canvasSize = Amethyst::UDim2(glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 600.0f));
    m_entityView->visible = false;
    m_entityView->markDirty();

    m_transformHeader = m_entityView->add<Amethyst::CollapsibleHeader>();
    m_transformHeader->title = "Transform";
    m_transformHeader->size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 124.0f);
    m_transformHeader->position = Amethyst::UDim2::fromOffset(0.0f, 4.0f);
    m_transformHeader->headerHeight = 28.0f;
    m_transformHeader->fontSize = 13.0f;
    m_transformHeader->markDirty();

    m_transformTable = m_transformHeader->add<Amethyst::Table>();
    m_transformTable->size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 88.0f);
    m_transformTable->position = Amethyst::UDim2::fromOffset(4.0f, 4.0f);
    m_transformTable->addColumn({"", Amethyst::TableColumnSizing::FIXED, 72.0f});
    m_transformTable->addColumn({"", Amethyst::TableColumnSizing::STRETCH, 1.0f});
    m_transformTable->rowHeight = 28.0f;
    m_transformTable->showHeader = false;
    m_transformTable->showColumnSeparators = false;
    m_transformTable->cellPadding = Amethyst::UDim4{
        Amethyst::UDim::fromOffset(0.0f),
        Amethyst::UDim::fromOffset(4.0f),
        Amethyst::UDim::fromOffset(0.0f),
        Amethyst::UDim::fromOffset(4.0f),
    };
    m_transformTable->markDirty();

    struct SliderConfig {
        const char *label;
        glm::vec3 min;
        glm::vec3 max;
        float speed;
    };
    SliderConfig configs[3] = {
        {"Translation", glm::vec3(-1000.0f), glm::vec3(1000.0f), 0.1f},
        {"Rotation", glm::vec3(-360.0f), glm::vec3(360.0f), 0.5f},
        {"Scale", glm::vec3(-100.0f), glm::vec3(100.0f), 0.01f},
    };

    for (int row = 0; row < 3; ++row) {
        m_transformTable->addRow();

        auto labelCell = std::make_unique<Amethyst::TextLabel>();
        labelCell->text = configs[row].label;
        labelCell->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
        labelCell->textColor = {0.7f, 0.7f, 0.7f, 1.0f};
        labelCell->fontSize = 12.0f;
        labelCell->backgroundTransparency = 1.0f;
        labelCell->textXAlignment = Amethyst::TextXAlignment::CENTER;
        labelCell->textYAlignment = Amethyst::TextYAlignment::CENTER;
        labelCell->markDirty();
        m_transformTable->nextCell(std::move(labelCell));

        auto sliderCell = std::make_unique<Amethyst::SliderVec3>();
        sliderCell->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
        sliderCell->valueRef = &m_transformValues[row];
        sliderCell->min = configs[row].min;
        sliderCell->max = configs[row].max;
        sliderCell->speed = configs[row].speed;
        sliderCell->label = "";
        sliderCell->valueColor = {0.9f, 0.9f, 0.9f, 1.0f};
        sliderCell->fontSize = 12.0f;
        sliderCell->markDirty();

        int capturedRow = row;
        sliderCell->onValueChanged = [this, capturedRow](glm::vec3 val) {
            if (!m_selectedEntity.isValid()) {
                return;
            }
            if (!m_selectedEntity.hasComponent<Rapture::TransformComponent>()) {
                return;
            }
            auto &tc = m_selectedEntity.getComponent<Rapture::TransformComponent>();
            if (capturedRow == 0) {
                tc.transforms.setTranslation(val);
            } else if (capturedRow == 1) {
                tc.transforms.setRotation(val);
            } else {
                tc.transforms.setScale(val);
            }
        };

        m_transformSliders[row] = static_cast<Amethyst::SliderVec3 *>(m_transformTable->nextCell(std::move(sliderCell)));
    }
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
    m_transformValues[0] = transform.translation();
    m_transformValues[1] = transform.rotation();
    m_transformValues[2] = transform.scale();

    for (int row = 0; row < 3; ++row) {
        m_transformSliders[row]->markDirty();
    }
}

void PropertiesPanel::showEntity(const Rapture::Entity &entity)
{
    m_selectedEntity = entity;
    m_placeholderText->visible = false;
    m_entityView->visible = true;
    m_placeholderText->markDirty();
    m_entityView->markDirty();
}

void PropertiesPanel::showPlaceholder()
{
    m_placeholderText->visible = true;
    m_entityView->visible = false;
    m_placeholderText->markDirty();
    m_entityView->markDirty();
}
