#include "OutlinerPanel.h"
#include "Icons.h"
#include "components/Components.h"
#include "components/HierarchyComponent.h"
#include "events/GameEvents.h"
#include "layers/panels/components/tab_layouts.h"
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
    m_root->addClass("panel");
    m_root->setBaseProperties({.clipsDescendants = true});

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
                        .automaticCanvasSize = Amethyst::AutomaticSize::Y,
                    },
            },
            [this](Amethyst::ScrollingFrameScope &sf) {
                m_scrollingFrame = &sf.component;
                sf.treeView(
                    {
                        .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
                        .treeView = {.cellPadding = {0},
                                     .showColumnSeparators = false,
                                     .disclosureTriangleSize = 24.0f,
                                     .showHeader = true},
                    },
                    [this](Amethyst::TreeViewScope &tv) {
                        m_treeView = &tv.component;
                        tv.column("Name", 3.0f);
                        tv.column("Type", 1.0f);
                        tv.column("", 0.5f);
                    });
            });

    m_hostTabBar->addTab(std::move(root), iconTabLayout("Outliner", Icons::SVG_LAYERS));
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

    Amethyst::TreeViewScope tvScope(*m_treeView);
    tvScope.columnsExplicit = true;

    m_scene->getRegistry().view<Rapture::TagComponent>().each([this, &tvScope](auto entityHandle, auto &tag) {
        Rapture::Entity entity(entityHandle, m_scene.get());

        bool isRoot = !entity.hasComponent<Rapture::HierarchyComponent>() ||
                      !entity.getComponent<Rapture::HierarchyComponent>().parent.isValid();
        if (!isRoot) {
            return;
        }

        tvScope.row([this, entity](Amethyst::TreeRowScope &row) { buildEntityTree(entity, row); });
    });
}

void OutlinerPanel::buildEntityTree(Rapture::Entity entity, Amethyst::TreeRowScope &rowScope)
{
    if (!entity.isValid()) {
        return;
    }

    std::string entityName = "Unnamed Entity";
    if (entity.hasComponent<Rapture::TagComponent>()) {
        entityName = entity.getComponent<Rapture::TagComponent>().tag;
    }

    uint32_t entityId = entity.getID();

    rowScope.cell([entityName, entityId](Amethyst::UIScope &s) {
        s.textButton(
            {
                .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f), .zIndex = 2},
                .style = {.backgroundTransparency = 1.0f, .borderTransparency = 1.0},
                .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
                .label = entityName,
            },
            [entityId](Amethyst::TextButtonScope &tb) {
                tb.component.onMouseButton1ClickCb = [entityId]() {
                    s_onEntityClicked(entityId);
                    return Amethyst::EventResult::CONSUMED;
                };
            });
    });

    rowScope.cell([](Amethyst::UIScope &s) {
        s.textLabel({
            .base = {.size = Amethyst::UDim2::fromScale(1.0f, 1.0f)},
            .style = {.backgroundTransparency = 1.0f},
            .text = {.textYAlignment = Amethyst::TextYAlignment::CENTER},
            .label = "Entity",
        });
    });

    if (entity.hasComponent<Rapture::HierarchyComponent>()) {
        const auto &hierarchy = entity.getComponent<Rapture::HierarchyComponent>();
        for (const auto &child : hierarchy.children) {
            if (child.isValid()) {
                rowScope.row([this, child](Amethyst::TreeRowScope &childRow) { buildEntityTree(child, childRow); });
            }
        }
    }
}
