#include "GraphEditor.h"
#include <imgui.h>
#include <algorithm>

namespace Modules {

GraphEditor::GraphEditor(const char *label, std::shared_ptr<Graph> graph, ImVec2 size)
    : m_label(label), m_graph(graph), m_size(size), m_scrolling(ImVec2(0.0f, 0.0f)), m_zoom(1.0f)
{
}

GraphEditor::~GraphEditor() {}

ImVec2 GraphEditor::canvasToScreen(ImVec2 canvasPos)
{
    ImVec2 panelPos = ImGui::GetWindowPos();
    return ImVec2(panelPos.x + m_scrolling.x + canvasPos.x * m_zoom,
                  panelPos.y + m_scrolling.y + canvasPos.y * m_zoom);
}

ImVec2 GraphEditor::screenToCanvas(ImVec2 screenPos)
{
    ImVec2 panelPos = ImGui::GetWindowPos();
    return ImVec2((screenPos.x - panelPos.x - m_scrolling.x) / m_zoom,
                  (screenPos.y - panelPos.y - m_scrolling.y) / m_zoom);
}

void GraphEditor::render()
{
    ImGui::BeginChild(m_label.c_str(), ImVec2(0, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        m_zoom = std::clamp(m_zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.3f, 3.0f);
    }

    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        m_scrolling.x += ImGui::GetIO().MouseDelta.x;
        m_scrolling.y += ImGui::GetIO().MouseDelta.y;
    }

    auto &nodes = m_graph->getNodes();
    for (auto &[nodeId, node] : nodes) {
        renderNode(node);
    }

    ImGui::EndChild();
}

void GraphEditor::renderNode(GraphNode &node)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    ImVec2 screenPos = canvasToScreen(node.windowPosition);
    ImVec2 screenSize = ImVec2(node.windowSize.x * m_zoom, node.windowSize.y * m_zoom);

    drawList->AddRectFilled(screenPos, ImVec2(screenPos.x + screenSize.x, screenPos.y + screenSize.y),
                           ImGui::ColorConvertFloat4ToU32(node.color), 4.0f);
    drawList->AddRect(screenPos, ImVec2(screenPos.x + screenSize.x, screenPos.y + screenSize.y),
                     IM_COL32(100, 100, 100, 255), 4.0f, 0, 2.0f);
    drawList->AddText(ImVec2(screenPos.x + 8, screenPos.y + 8), IM_COL32_WHITE, node.name.c_str());

    ImGui::PushID(node.id);
    ImGui::SetCursorScreenPos(screenPos);
    ImGui::InvisibleButton("##node", screenSize);

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        node.windowPosition.x += delta.x / m_zoom;
        node.windowPosition.y += delta.y / m_zoom;
    }

    float handleSize = 16.0f;
    ImVec2 resizeScreenPos = ImVec2(screenPos.x + screenSize.x - handleSize, screenPos.y + screenSize.y - handleSize);
    ImGui::SetCursorScreenPos(resizeScreenPos);
    ImGui::InvisibleButton("##resize", ImVec2(handleSize, handleSize));

    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        node.windowSize.x = std::max(100.0f, node.windowSize.x + delta.x / m_zoom);
        node.windowSize.y = std::max(60.0f, node.windowSize.y + delta.y / m_zoom);
    }

    ImGui::PopID();
}

uint32_t GraphEditor::addNode(const GraphNode &node) { return m_graph->addNode(node); }
bool GraphEditor::removeNode(uint32_t nodeId) { return m_graph->removeNode(nodeId); }

} // namespace Modules
