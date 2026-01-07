#include "GraphEditor.h"
#include <algorithm>
#include <glm/fwd.hpp>
#include <imgui.h>

namespace Modules {

constexpr float MIN_NODE_WIDTH = 150.0f;

ImVec4 s_paramTypeToColor(ParameterType type)
{

    switch (type) {
    case U32:
        return {1.00f, 0.00f, 0.00f, 1.0f};
    case U64:
        return {0.00f, 1.00f, 0.00f, 1.0f};
    case UVEC2:
        return {0.00f, 0.00f, 1.00f, 1.0f};
    case UVEC3:
        return {1.00f, 1.00f, 0.00f, 1.0f};
    case UVEC4:
        return {1.00f, 0.00f, 1.00f, 1.0f};

    case I32:
        return {0.00f, 1.00f, 1.00f, 1.0f};
    case I64:
        return {1.00f, 0.50f, 0.00f, 1.0f};
    case IVEC2:
        return {0.50f, 0.00f, 1.00f, 1.0f};
    case IVEC3:
        return {0.00f, 0.50f, 1.00f, 1.0f};
    case IVEC4:
        return {0.50f, 1.00f, 0.00f, 1.0f};

    case F32:
        return {1.00f, 0.00f, 0.50f, 1.0f};
    case F64:
        return {0.00f, 1.00f, 0.50f, 1.0f};
    case VEC2:
        return {0.50f, 0.00f, 0.00f, 1.0f};
    case VEC3:
        return {0.00f, 0.50f, 0.00f, 1.0f};
    case VEC4:
        return {0.00f, 0.00f, 0.50f, 1.0f};

    case STRING:
        return {0.75f, 0.75f, 0.75f, 1.0f};
    case TEXTURE_HANDLE:
        return {0.25f, 0.25f, 0.25f, 1.0f};

    default:
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

GraphEditor::GraphEditor(const char *label, std::shared_ptr<Graph> graph, ImVec2 size)
    : m_label(label), m_graph(graph), m_size(size), m_scrolling(ImVec2(0.0f, 0.0f)), m_zoom(1.0f), m_isDraggingConnection(false),
      m_isOutputPin(false), m_connectionSourceNode(0), m_connectionSourceParam(0), m_connectionDragPos(ImVec2(0.0f, 0.0f))
{
}

GraphEditor::~GraphEditor() {}

ImVec2 GraphEditor::canvasToScreen(ImVec2 canvasPos)
{
    ImVec2 panelPos = ImGui::GetWindowPos();
    return ImVec2(panelPos.x + m_scrolling.x + canvasPos.x * m_zoom, panelPos.y + m_scrolling.y + canvasPos.y * m_zoom);
}

ImVec2 GraphEditor::screenToCanvas(ImVec2 screenPos)
{
    ImVec2 panelPos = ImGui::GetWindowPos();
    return ImVec2((screenPos.x - panelPos.x - m_scrolling.x) / m_zoom, (screenPos.y - panelPos.y - m_scrolling.y) / m_zoom);
}

ImVec2 GraphEditor::getParameterPinPosition(uint32_t nodeId, uint32_t paramIndex, bool isInput)
{
    auto &nodes = m_graph->getNodes();
    auto it = nodes.find(nodeId);
    if (it == nodes.end()) return ImVec2(0, 0);

    auto &node = it->second;
    float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    float headerHeight = lineHeight + 16.0f;
    float paramY = node.windowPosition.y + headerHeight + 8.0f + paramIndex * lineHeight + lineHeight * 0.5f;

    ImVec2 canvasPos;
    if (isInput) {
        canvasPos = ImVec2(node.windowPosition.x, paramY);
    } else {
        canvasPos = ImVec2(node.windowPosition.x + node.windowSize.x, paramY);
    }

    return canvasToScreen(canvasPos);
}

void GraphEditor::renderGrid()
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 panelPos = ImGui::GetWindowPos();
    ImVec2 panelSize = ImGui::GetWindowSize();

    // Calculate grid spacing based on zoom - aim for roughly 100px at zoom 1.0
    float baseSpacing = 100.0f;
    float majorGridSize = baseSpacing * m_zoom;
    float minorGridSize = majorGridSize * 0.25f;

    // Adjust to powers of 2 or nice round numbers for visual stability
    while (majorGridSize < 50.0f) majorGridSize *= 2.0f;
    while (majorGridSize > 200.0f) majorGridSize *= 0.5f;
    minorGridSize = majorGridSize * 0.25f;

    // Calculate grid offset based on scrolling
    float offsetX = fmodf(m_scrolling.x, majorGridSize);
    float offsetY = fmodf(m_scrolling.y, majorGridSize);
    float minorOffsetX = fmodf(m_scrolling.x, minorGridSize);
    float minorOffsetY = fmodf(m_scrolling.y, minorGridSize);

    // Draw minor grid (more transparent)
    for (float x = offsetX - minorGridSize; x < panelSize.x; x += minorGridSize) {
        drawList->AddLine(ImVec2(panelPos.x + x, panelPos.y), ImVec2(panelPos.x + x, panelPos.y + panelSize.y),
                          IM_COL32(80, 80, 80, 40), 1.0f);
    }
    for (float y = offsetY - minorGridSize; y < panelSize.y; y += minorGridSize) {
        drawList->AddLine(ImVec2(panelPos.x, panelPos.y + y), ImVec2(panelPos.x + panelSize.x, panelPos.y + y),
                          IM_COL32(80, 80, 80, 40), 1.0f);
    }

    // Draw major grid (less transparent)
    for (float x = offsetX - majorGridSize; x < panelSize.x; x += majorGridSize) {
        drawList->AddLine(ImVec2(panelPos.x + x, panelPos.y), ImVec2(panelPos.x + x, panelPos.y + panelSize.y),
                          IM_COL32(100, 100, 100, 80), 1.0f);
    }
    for (float y = offsetY - majorGridSize; y < panelSize.y; y += majorGridSize) {
        drawList->AddLine(ImVec2(panelPos.x, panelPos.y + y), ImVec2(panelPos.x + panelSize.x, panelPos.y + y),
                          IM_COL32(100, 100, 100, 80), 1.0f);
    }
}

void GraphEditor::render()
{
    ImGui::BeginChild(m_label.c_str(), ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        m_zoom = std::clamp(m_zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.3f, 3.0f);
    }

    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        m_scrolling.x += ImGui::GetIO().MouseDelta.x;
        m_scrolling.y += ImGui::GetIO().MouseDelta.y;
    }

    renderGrid();
    renderConnections();

    auto &nodes = m_graph->getNodes();
    for (auto &[nodeId, node] : nodes) {
        renderNode(node);
    }

    if (m_isDraggingConnection) {
        m_connectionDragPos = ImGui::GetMousePos();
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_isDraggingConnection = false;
        }
    }

    ImGui::EndChild();
}

void GraphEditor::renderConnections()
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    auto &nodes = m_graph->getNodes();
    for (auto &[nodeId, node] : nodes) {
        for (auto &conn : node.connections) {
            if (conn.fromNode != nodeId) continue;

            ImVec2 p1 = getParameterPinPosition(conn.fromNode, conn.outputIndex, false);
            ImVec2 p2 = getParameterPinPosition(conn.toNode, conn.inputIndex, true);
            drawList->AddLine(p1, p2, IM_COL32(200, 200, 200, 255), 3.0f * m_zoom);
        }
    }

    if (m_isDraggingConnection) {
        ImVec2 startPos = getParameterPinPosition(m_connectionSourceNode, m_connectionSourceParam, !m_isOutputPin);
        drawList->AddLine(startPos, m_connectionDragPos, IM_COL32(200, 200, 200, 255), 3.0f * m_zoom);
    }
}

void GraphEditor::renderNode(GraphNode &node)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    float headerHeight = lineHeight + 16.0f;
    float numParams = std::max(node.inputs.size(), node.outputs.size());
    float contentHeight = numParams * lineHeight + 16.0f;
    node.windowSize.y = headerHeight + contentHeight;

    ImVec2 screenPos = canvasToScreen(node.windowPosition);
    ImVec2 screenSize = ImVec2(node.windowSize.x * m_zoom, node.windowSize.y * m_zoom);

    drawList->AddRectFilled(screenPos, ImVec2(screenPos.x + screenSize.x, screenPos.y + screenSize.y),
                            ImGui::ColorConvertFloat4ToU32(node.color), 4.0f);
    drawList->AddRect(screenPos, ImVec2(screenPos.x + screenSize.x, screenPos.y + screenSize.y), IM_COL32(100, 100, 100, 255), 4.0f,
                      0, 2.0f);

    drawList->AddRectFilled(
        screenPos, ImVec2(screenPos.x + screenSize.x, screenPos.y + headerHeight * m_zoom),
        ImGui::ColorConvertFloat4ToU32(ImVec4(node.color.x * 0.7f, node.color.y * 0.7f, node.color.z * 0.7f, 1.0f)), 4.0f,
        ImDrawFlags_RoundCornersTop);

    float textScale = std::clamp(m_zoom, 0.7f, 1.5f);
    ImVec2 titleSize = ImGui::CalcTextSize(node.name.c_str());
    float titleX = screenPos.x + (screenSize.x - titleSize.x * textScale) * 0.5f;
    float titleY = screenPos.y + 8.0f * m_zoom;
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * textScale, ImVec2(titleX, titleY), IM_COL32_WHITE,
                      node.name.c_str());

    renderInputPins(node, screenPos, headerHeight, lineHeight, textScale);
    renderOutputPins(node, screenPos, headerHeight, lineHeight, textScale);
    handleNodeInteraction(node, screenPos, screenSize, headerHeight);
}

void GraphEditor::renderInputPins(GraphNode &node, ImVec2 screenPos, float headerHeight, float lineHeight, float textScale)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    float currentY = headerHeight + 8.0f;

    for (size_t i = 0; i < node.inputs.size(); ++i) {
        ImVec2 pinCanvasPos = ImVec2(node.windowPosition.x, node.windowPosition.y + currentY + lineHeight * 0.5f);
        ImVec2 pinScreenPos = canvasToScreen(pinCanvasPos);
        float pinRadius = 6.0f * m_zoom;

        bool hasConnection = false;
        for (auto &conn : node.connections) {
            if (conn.toNode == node.id && conn.inputIndex == i) {
                hasConnection = true;
                break;
            }
        }

        auto pinColor = ImGui::ColorConvertFloat4ToU32(s_paramTypeToColor(node.inputs[i].pType));
        if (hasConnection) {
            drawList->AddCircleFilled(pinScreenPos, pinRadius, pinColor);
        } else {
            drawList->AddCircle(pinScreenPos, pinRadius, pinColor, 12, 2.0f);
        }

        ImVec2 labelPos = ImVec2(pinScreenPos.x + pinRadius + 4.0f * m_zoom, screenPos.y + currentY * m_zoom);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * textScale, labelPos, IM_COL32(220, 220, 220, 255),
                          node.inputs[i].name.c_str());

        ImGui::PushID(1000 + (int)i);
        ImGui::SetCursorScreenPos(ImVec2(pinScreenPos.x - pinRadius, pinScreenPos.y - pinRadius));
        ImGui::InvisibleButton("##pin", ImVec2(pinRadius * 2, pinRadius * 2));

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            m_isDraggingConnection && m_isOutputPin) {
            NodeConnection conn{m_connectionSourceNode, node.id, m_connectionSourceParam, (uint32_t)i};
            m_graph->link(conn);
            m_isDraggingConnection = false;
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !m_isDraggingConnection) {
            m_isDraggingConnection = true;
            m_isOutputPin = false;
            m_connectionSourceNode = node.id;
            m_connectionSourceParam = i;
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            std::vector<NodeConnection> toRemove;
            for (auto &conn : node.connections) {
                if (conn.toNode == node.id && conn.inputIndex == i) {
                    toRemove.push_back(conn);
                }
            }
            for (auto &conn : toRemove) {
                m_graph->unlink(conn);
            }
        }

        ImGui::PopID();
        currentY += lineHeight;
    }
}

void GraphEditor::renderOutputPins(GraphNode &node, ImVec2 screenPos, float headerHeight, float lineHeight, float textScale)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    float currentY = headerHeight + 8.0f;

    for (size_t i = 0; i < node.outputs.size(); ++i) {
        ImVec2 pinCanvasPos =
            ImVec2(node.windowPosition.x + node.windowSize.x, node.windowPosition.y + currentY + lineHeight * 0.5f);
        ImVec2 pinScreenPos = canvasToScreen(pinCanvasPos);
        float pinRadius = 6.0f * m_zoom;

        bool hasConnection = false;
        for (auto &conn : node.connections) {
            if (conn.fromNode == node.id && conn.outputIndex == i) {
                hasConnection = true;
                break;
            }
        }

        auto pinColor = ImGui::ColorConvertFloat4ToU32(s_paramTypeToColor(node.outputs[i].pType));
        if (hasConnection) {
            drawList->AddCircleFilled(pinScreenPos, pinRadius, pinColor);
        } else {
            drawList->AddCircle(pinScreenPos, pinRadius, pinColor, 12, 2.0f);
        }

        ImVec2 textSize = ImGui::CalcTextSize(node.outputs[i].name.c_str());
        ImVec2 labelPos =
            ImVec2(pinScreenPos.x - pinRadius - 4.0f * m_zoom - textSize.x * textScale, screenPos.y + currentY * m_zoom);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * textScale, labelPos, IM_COL32(220, 220, 220, 255),
                          node.outputs[i].name.c_str());

        ImGui::PushID(2000 + (int)i);
        ImGui::SetCursorScreenPos(ImVec2(pinScreenPos.x - pinRadius, pinScreenPos.y - pinRadius));
        ImGui::InvisibleButton("##pin", ImVec2(pinRadius * 2, pinRadius * 2));

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            m_isDraggingConnection && !m_isOutputPin) {
            NodeConnection conn{node.id, m_connectionSourceNode, (uint32_t)i, m_connectionSourceParam};
            m_graph->link(conn);
            m_isDraggingConnection = false;
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !m_isDraggingConnection) {
            m_isDraggingConnection = true;
            m_isOutputPin = true;
            m_connectionSourceNode = node.id;
            m_connectionSourceParam = i;
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            std::vector<NodeConnection> toRemove;
            for (auto &conn : node.connections) {
                if (conn.fromNode == node.id && conn.outputIndex == i) {
                    toRemove.push_back(conn);
                }
            }
            for (auto &conn : toRemove) {
                m_graph->unlink(conn);
            }
        }

        ImGui::PopID();
        currentY += lineHeight;
    }
}

void GraphEditor::handleNodeInteraction(GraphNode &node, ImVec2 screenPos, ImVec2 screenSize, float headerHeight)
{
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImGui::PushID(node.id);
    ImGui::SetCursorScreenPos(screenPos);
    ImGui::InvisibleButton("##header", ImVec2(screenSize.x, headerHeight * m_zoom));

    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !m_isDraggingConnection) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        node.windowPosition.x += delta.x / m_zoom;
        node.windowPosition.y += delta.y / m_zoom;
    }

    float handleSize = 16.0f * m_zoom;

    ImGui::SetCursorScreenPos(ImVec2(screenPos.x, screenPos.y));
    ImGui::InvisibleButton("##resize_left", ImVec2(handleSize, screenSize.y));
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        float canvasDelta = delta.x / m_zoom;
        float newWidth = std::max(MIN_NODE_WIDTH, node.windowSize.x - canvasDelta);
        float actualChange = node.windowSize.x - newWidth;
        node.windowPosition.x += actualChange;
        node.windowSize.x = newWidth;
    }

    ImGui::SetCursorScreenPos(ImVec2(screenPos.x + screenSize.x - handleSize, screenPos.y));
    ImGui::InvisibleButton("##resize_right", ImVec2(handleSize, screenSize.y));
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        node.windowSize.x = std::max(MIN_NODE_WIDTH, node.windowSize.x + delta.x / m_zoom);
    }

    // Draw resize indicator lines
    for (int i = 1; i <= 3; ++i) {
        float offset = i * 5.0f;
        drawList->AddLine(ImVec2(screenPos.x + screenSize.x - offset, screenPos.y + screenSize.y),
                          ImVec2(screenPos.x + screenSize.x, screenPos.y + screenSize.y - offset), IM_COL32(150, 150, 150, 255),
                          1.5f);
    }

    ImGui::PopID();
}

uint32_t GraphEditor::addNode(const GraphNode &node)
{
    return m_graph->addNode(node);
}
bool GraphEditor::removeNode(uint32_t nodeId)
{
    return m_graph->removeNode(nodeId);
}

} // namespace Modules
