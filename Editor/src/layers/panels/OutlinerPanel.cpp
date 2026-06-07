#include "OutlinerPanel.h"
#include "components/Components.h"
#include "components/HierarchyComponent.h"
#include "events/GameEvents.h"
#include "scenes/SceneManager.h"
#include "scenes/entities/Entity.h"

#include <components/ui_scope.h>

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

OutlinerPanel::OutlinerPanel(Amethyst::TabBar *tabBar) : m_hostTabBar(tabBar)
{
    auto root = std::make_unique<Amethyst::Frame>();
    m_root = root.get();
    m_root->name = "Outliner";

    Amethyst::UIScope(*m_root)
        .frame({
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
        })
        .scrollingFrame(
            {
                .base =
                    {
                        .clipsDescendants = true,
                        .size = Amethyst::UDim2::fromScale(1.0f, 1.0f),
                    },
                .scroll =
                    {
                        .scrollAxis = Amethyst::ScrollAxis::Y,
                        .scrollBarVisibility = Amethyst::ScrollBarVisibility::AUTO,
                        .canvasSize = Amethyst::UDim2::fromScale(1.0f, 3.0f),
                    },
            },
            [this](Amethyst::ScrollingFrameScope &sf) {
                m_scrollingFrame = &sf.component;
                sf.treeView(
                    {
                        .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                        .treeView = {.showColumnSeparators = false},
                    },
                    [this](Amethyst::TreeViewScope &tv) {
                        m_treeView = &tv.component;
                        tv.column("", 1.0f);
                    });
            });

    m_hostTabBar->addTab(std::move(root), "Outliner");
}

OutlinerPanel::~OutlinerPanel()
{
    if (m_hostTabBar != nullptr && m_root != nullptr) {
        m_hostTabBar->removeTab(m_root);
    }
}

void OutlinerPanel::setScene(std::shared_ptr<Rapture::Scene> scene)
{
    m_scene = scene;
    m_hasScene = (scene != nullptr);

    if (m_hasScene) {
        m_treeView->setBaseProperties({.visible = true});
        refresh();
    } else {
        m_treeView->setBaseProperties({.visible = false});
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
            buildEntityTree(entity, 0);
        } else {
            const auto &hierarchy = entity.getComponent<Rapture::HierarchyComponent>();
            if (!hierarchy.parent.isValid()) {
                buildEntityTree(entity, 0);
            }
        }
    });
}

void OutlinerPanel::buildEntityTree(Rapture::Entity entity, uint16_t depth)
{
    if (!entity.isValid()) {
        return;
    }

    m_treeView->addRow(depth);

    std::string entityName = "Unnamed Entity";
    if (entity.hasComponent<Rapture::TagComponent>()) {
        entityName = entity.getComponent<Rapture::TagComponent>().tag;
    }

    uint32_t entityId = entity.getID();

    auto btn = std::make_unique<Amethyst::TextButton>();
    btn->setBaseProperties({.size = Amethyst::UDim2::fromScale(1.0f, 1.0f), .zIndex = 2});
    btn->setBaseStyleProperties({.backgroundTransparency = 1.0f});
    btn->setTextStyleProperties({.textYAlignment = Amethyst::TextYAlignment::CENTER});
    btn->setText(entityName);
    btn->name = std::to_string(entityId);
    btn->onMouseButton1ClickCb = [entityId]() {
        s_onEntityClicked(entityId);
        return Amethyst::EventResult::CONSUMED;
    };
    m_treeView->nextCell(std::move(btn));

    if (entity.hasComponent<Rapture::HierarchyComponent>()) {
        const auto &hierarchy = entity.getComponent<Rapture::HierarchyComponent>();
        for (const auto &child : hierarchy.children) {
            if (child.isValid()) {
                buildEntityTree(child, static_cast<uint16_t>(depth + 1));
            }
        }
    }
}
