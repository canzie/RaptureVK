#ifndef RAPTURE__GRAPH_EDITOR_H
#define RAPTURE__GRAPH_EDITOR_H

#include "Graph.h"
#include <imgui.h>
#include <memory>

namespace Modules {

class GraphEditor {
  public:
    GraphEditor(const char *label, std::shared_ptr<Graph> graph, ImVec2 size = ImVec2(800, 600));
    ~GraphEditor();

    void render();

    uint32_t addNode(const GraphNode &node);
    bool removeNode(uint32_t nodeId);

  private:
    void renderGrid();
    void renderNode(GraphNode &node);
    void renderConnections();
    void renderInputPins(GraphNode &node, ImVec2 screenPos, float headerHeight, float lineHeight, float textScale);
    void renderOutputPins(GraphNode &node, ImVec2 screenPos, float headerHeight, float lineHeight, float textScale);
    void handleNodeInteraction(GraphNode &node, ImVec2 screenPos, ImVec2 screenSize, float headerHeight);

    ImVec2 canvasToScreen(ImVec2 canvasPos);
    ImVec2 screenToCanvas(ImVec2 screenPos);
    ImVec2 getParameterPinPosition(uint32_t nodeId, uint32_t paramIndex, bool isInput);

  private:
    std::string m_label;
    std::shared_ptr<Graph> m_graph;
    ImVec2 m_size;

    ImVec2 m_scrolling;
    float m_zoom;

    bool m_isDraggingConnection;
    bool m_isOutputPin;
    uint32_t m_connectionSourceNode;
    uint32_t m_connectionSourceParam;
    ImVec2 m_connectionDragPos;
};

} // namespace Modules

#endif // RAPTURE__GRAPH_EDITOR_H
