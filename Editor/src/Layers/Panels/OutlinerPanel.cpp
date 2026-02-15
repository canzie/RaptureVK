#include "OutlinerPanel.h"
#include "Components/Components.h"
#include "Components/HierarchyComponent.h"
#include "Events/GameEvents.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/SceneManager.h"
#include <components/common.h>
#include <modules/color.h>

static void s_onEntityClicked(uint32_t entityId)
{
    auto scene = Rapture::SceneManager::getInstance().getActiveScene();
    if (!scene) {
        return;
    }

    Rapture::Entity entity(entityId, scene.get());
    if (entity.isValid()) {
        Rapture::GameEvents::onEntitySelected().publish(std::make_shared<Rapture::Entity>(entity));
    }
}

OutlinerPanel::OutlinerPanel(Amethyst::DockingLayer *dockingLayer) : m_dockingLayer(dockingLayer)
{
    auto root = std::make_unique<Amethyst::PanelLayer>();
    m_root = root.get();
    m_root->name = "Outliner";
    m_root->markDirty();

    m_background = m_root->add<Amethyst::Frame>();
    m_background->name = "Outliner Background";
    m_background->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
    m_background->markDirty();

    m_scrollingFrame = m_root->add<Amethyst::ScrollingFrame>();
    m_scrollingFrame->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
    m_scrollingFrame->clipsDescendants = true;
    m_scrollingFrame->scrollAxis = Amethyst::ScrollAxis::Y;
    m_scrollingFrame->scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO;
    m_scrollingFrame->canvasSize = Amethyst::UDim2::fromScale(1.0f, 3.0f);
    m_scrollingFrame->markDirty();

    m_treeView = m_scrollingFrame->add<Amethyst::TreeView>();
    m_treeView->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
    m_treeView->numCols = 1;
    m_treeView->showColumnSeparators = false;
    m_treeView->backgroundTransparency = 1.0f;
    m_treeView->markDirty();

    m_dockingLayer->dock(std::move(root), glm::vec2(0.0f));
}

OutlinerPanel::~OutlinerPanel()
{
    if (m_dockingLayer && m_root) {
        m_dockingLayer->undock(m_root);
    }
}

void OutlinerPanel::setScene(std::shared_ptr<Rapture::Scene> scene)
{
    m_scene = scene;
    m_hasScene = (scene != nullptr);

    if (m_hasScene) {
        m_treeView->visible = true;
        refresh();
    } else {
        m_treeView->visible = false;
        m_treeView->clear();
    }
}

void OutlinerPanel::refresh()
{
    if (!m_hasScene || !m_scene) {
        return;
    }

    m_treeView->clear();

    m_scene->getRegistry().view<Rapture::TagComponent>().each([this](auto entityHandle, auto &tag) {
        Rapture::Entity entity(entityHandle, m_scene.get());

        if (!entity.hasComponent<Rapture::HierarchyComponent>()) {
            buildEntityTree(entity, Amethyst::INVALID_ROW);
        } else {
            auto &hierarchy = entity.getComponent<Rapture::HierarchyComponent>();
            if (!hierarchy.parent.isValid()) {
                buildEntityTree(entity, Amethyst::INVALID_ROW);
            }
        }
    });

    m_treeView->markDirty();
}

void OutlinerPanel::buildEntityTree(Rapture::Entity entity, uint32_t parentRow)
{
    if (!entity.isValid()) {
        return;
    }

    uint32_t rowIndex = m_treeView->beginRow(parentRow);

    std::string entityName = "Unnamed Entity";
    if (entity.hasComponent<Rapture::TagComponent>()) {
        entityName = entity.getComponent<Rapture::TagComponent>().tag;
    }

    uint32_t entityId = entity.getID();

    auto *button = m_treeView->add<Amethyst::TextButton>();
    button->text = entityName;
    button->name = std::to_string(entityId);
    button->backgroundTransparency = 1.0f;
    button->textYAlignment = Amethyst::TextYAlignment::CENTER;
    button->size = Amethyst::UDim2::fromScale(1.0f, 1.0f);
    button->zIndex = 2;
    button->onMouseButton1ClickCb = [entityId]() { s_onEntityClicked(entityId); };
    button->markDirty();

    m_treeView->endRow();

    if (entity.hasComponent<Rapture::HierarchyComponent>()) {
        const auto &hierarchy = entity.getComponent<Rapture::HierarchyComponent>();
        for (const auto &child : hierarchy.children) {
            if (child.isValid()) {
                buildEntityTree(child, rowIndex);
            }
        }
    }
}
