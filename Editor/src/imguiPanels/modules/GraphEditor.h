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
    void renderNode(GraphNode &node);
    ImVec2 canvasToScreen(ImVec2 canvasPos);
    ImVec2 screenToCanvas(ImVec2 screenPos);

  private:
    std::string m_label;
    std::shared_ptr<Graph> m_graph;
    ImVec2 m_size;

    ImVec2 m_scrolling;
    float m_zoom;
};

} // namespace Modules

#endif // RAPTURE__GRAPH_EDITOR_H
