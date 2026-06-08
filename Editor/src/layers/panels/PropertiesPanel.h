#ifndef RAPTURE__PROPERTIES_PANEL_H
#define RAPTURE__PROPERTIES_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/table.h>
#include <components/ui_scope.h>

#include "layers/panels/Panel.h"
#include "scenes/Scene.h"
#include "scenes/entities/Entity.h"
#include <functional>
#include <memory>
#include <vector>

class PropertiesPanel : public Panel {
  public:
    PropertiesPanel(Amethyst::TabBar *tabBar);
    ~PropertiesPanel();
    PropertiesPanel(const PropertiesPanel &) = delete;
    PropertiesPanel &operator=(const PropertiesPanel &) = delete;
    PropertiesPanel(PropertiesPanel &&) = delete;
    PropertiesPanel &operator=(PropertiesPanel &&) = delete;

    void setScene(std::shared_ptr<Rapture::Scene> scene);
    void onUpdate(float dt) override;

  private:
    /**
     * @brief Pairs a collapsible header with the component it represents.
     *
     * A section is only shown when the selected entity owns the matching
     * component. The body height is used to stack visible sections.
     */
    struct ComponentSection {
        Amethyst::CollapsibleHeader *header = nullptr;
        float bodyHeight = 0.0f;
        std::function<bool(const Rapture::Entity &)> matches;
    };

    void setupSearchBar();
    void setupPlaceholder();
    void setupEntityView();

    void setupTransformHeader(Amethyst::ScrollingFrameScope &sf);
    void setupMeshHeader(Amethyst::ScrollingFrameScope &sf);
    void setupMaterialHeader(Amethyst::ScrollingFrameScope &sf);
    void setupLightHeader(Amethyst::ScrollingFrameScope &sf);
    void setupCameraHeader(Amethyst::ScrollingFrameScope &sf);
    void setupShadowHeader(Amethyst::ScrollingFrameScope &sf);
    void setupCascadedShadowHeader(Amethyst::ScrollingFrameScope &sf);
    void setupSkyboxHeader(Amethyst::ScrollingFrameScope &sf);
    void setupTerrainHeader(Amethyst::ScrollingFrameScope &sf);

    /**
     * @brief Creates a collapsible header, registers it as a section and wires
     * up relayout on toggle. The body callback fills the expanded content.
     */
    Amethyst::CollapsibleHeader *beginSection(Amethyst::ScrollingFrameScope &sf, const char *title, float bodyHeight,
                                              std::function<bool(const Rapture::Entity &)> matches,
                                              std::function<void(Amethyst::CollapsibleHeaderScope &)> body);
    void addStubBody(Amethyst::CollapsibleHeaderScope &ch);

    void relayout();
    void showEntity(const Rapture::Entity &entity);
    void showPlaceholder();

  private:
    Amethyst::TabBar *m_hostTabBar = nullptr;
    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextLabel *m_placeholderText = nullptr;
    Amethyst::ScrollingFrame *m_entityView = nullptr;
    Amethyst::TextInput *m_searchInput = nullptr;
    Amethyst::CollapsibleHeader *m_transformHeader = nullptr;
    Amethyst::Table *m_transformTable = nullptr;
    Amethyst::SliderVec3 *m_transformSliders[3] = {};
    Amethyst::vec3 m_transformValues[3] = {};

    std::vector<ComponentSection> m_sections;

    std::shared_ptr<Rapture::Scene> m_scene;
    Rapture::Entity m_selectedEntity;
    size_t m_entitySelectedListenerID = 0;
};

#endif // RAPTURE__PROPERTIES_PANEL_H
