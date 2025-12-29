#ifndef RAPTURE__GRAPH_EDITOR_PANEL_H
#define RAPTURE__GRAPH_EDITOR_PANEL_H

#include "imguiPanels/modules/Graph.h"
#include "imguiPanels/modules/GraphEditor.h"
#include <memory>

class GraphEditorPanel {
  public:
    GraphEditorPanel();
    ~GraphEditorPanel();

    void render();

  private:
    void setupDemoGraph();

  private:
    std::shared_ptr<Modules::Graph> m_graph;
    std::unique_ptr<Modules::GraphEditor> m_editor;
};

#endif // RAPTURE__GRAPH_EDITOR_PANEL_H
