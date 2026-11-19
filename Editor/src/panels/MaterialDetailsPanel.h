#ifndef RAPTURE__MATERIAL_DETAILS_PANEL_H
#define RAPTURE__MATERIAL_DETAILS_PANEL_H

#include "inspector/PropertySections.h"
#include "panels/Panel.h"

#include <rapture/assets/asset_manager/Asset.h>
#include <rapture/core/events/EventSignal.h>

#include <cstdint>
#include <optional>

class NodeEditorPanel;

/**
 * @brief The settings of the open material, or of the node selected in its graph
 */
class MaterialDetailsPanel : public Panel {
  public:
    std::string_view typeName() const override { return "MaterialDetailsPanel"; }
    MaterialDetailsPanel(const WorkspaceContext &context, Rapture::AssetHandle material, NodeEditorPanel &graph);

    void onUpdate(float dt) override;

  protected:
    void build(Amethyst::UIScope &scope) override;

  private:
    /**
     * @brief Shows a node, or the material when there is none
     * @param nodeId The node to show, 0 for the material
     */
    void showNode(uint32_t nodeId);

  private:
    Rapture::AssetRef m_material;
    NodeEditorPanel &m_graph;
    std::optional<PropertySectionList> m_sections;
    uint32_t m_shownNode = 0;
    bool m_rebuildPending = false;

    Rapture::EventConnection m_primaryNodeConn;
    Rapture::EventConnection m_nodeEditedConn;
    Rapture::EventConnection m_nodeValuesConn;
};

#endif // RAPTURE__MATERIAL_DETAILS_PANEL_H
