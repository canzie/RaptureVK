#include "PlotEditor.h"

#include <algorithm>
#include <cmath>

namespace Modules {

SplinePoints createSplinePoints(std::vector<glm::vec2> *points, InterpolationType interpolationType)
{
    SplinePoints splinePoints;
    splinePoints.points = points;
    splinePoints.interpolationType = interpolationType;
    return splinePoints;
}

static float s_evaluateSpline(const std::vector<glm::vec2> &pts, float x)
{
    if (pts.empty()) return 0.0f;
    if (pts.size() == 1) return pts[0].y;
    if (x <= pts.front().x) return pts.front().y;
    if (x >= pts.back().x) return pts.back().y;

    for (size_t i = 0; i < pts.size() - 1; ++i) {
        if (x < pts[i + 1].x) {
            float t = (x - pts[i].x) / (pts[i + 1].x - pts[i].x);
            return pts[i].y + t * (pts[i + 1].y - pts[i].y);
        }
    }
    return pts.back().y;
}

bool plotEditor(const char *label, const SplinePoints &splinePoints, ImVec2 size, float minX, float maxX, float minY, float maxY)
{
    if (splinePoints.points == nullptr) return false;

    std::vector<glm::vec2> &points = *splinePoints.points;

    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    ImGui::PushID(label);

    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Double-click to add points");
        ImGui::Text("Drag to move points");
        ImGui::Text("Right-click to delete points");
        ImGui::EndTooltip();
    }

    bool modified = false;

    if (size.x <= 0.0f) {
        size.x = ImGui::GetContentRegionAvail().x;
    }

    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = size;
    const ImVec2 canvasEnd = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

    const ImU32 colorBg = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    const ImU32 colorGrid = ImGui::GetColorU32(ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    const ImU32 colorCurve = ImGui::GetColorU32(ImVec4(0.8f, 0.8f, 0.2f, 1.0f));
    const ImU32 colorPoint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const ImU32 colorPointHover = ImGui::GetColorU32(ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
    const ImU32 colorPointActive = ImGui::GetColorU32(ImVec4(1.0f, 0.2f, 0.2f, 1.0f));

    drawList->AddRectFilled(canvasPos, canvasEnd, colorBg);

    const int gridDivisions = 8;
    for (int i = 0; i <= gridDivisions; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(gridDivisions);
        float x = canvasPos.x + t * canvasSize.x;
        float y = canvasPos.y + t * canvasSize.y;
        drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasEnd.y), colorGrid);
        drawList->AddLine(ImVec2(canvasPos.x, y), ImVec2(canvasEnd.x, y), colorGrid);
    }

    auto worldToCanvas = [&](float wx, float wy) -> ImVec2 {
        float nx = (wx - minX) / (maxX - minX);
        float ny = 1.0f - (wy - minY) / (maxY - minY);
        return ImVec2(canvasPos.x + nx * canvasSize.x, canvasPos.y + ny * canvasSize.y);
    };

    auto canvasToWorld = [&](ImVec2 canvas) -> glm::vec2 {
        float nx = (canvas.x - canvasPos.x) / canvasSize.x;
        float ny = 1.0f - (canvas.y - canvasPos.y) / canvasSize.y;
        float wx = minX + nx * (maxX - minX);
        float wy = minY + ny * (maxY - minY);
        return glm::vec2(wx, wy);
    };

    if (points.size() >= 2) {
        const int curveSegments = 100;
        for (int i = 0; i < curveSegments; ++i) {
            float t0 = static_cast<float>(i) / static_cast<float>(curveSegments);
            float t1 = static_cast<float>(i + 1) / static_cast<float>(curveSegments);
            float x0 = minX + t0 * (maxX - minX);
            float x1 = minX + t1 * (maxX - minX);
            float y0 = s_evaluateSpline(points, x0);
            float y1 = s_evaluateSpline(points, x1);
            ImVec2 p0 = worldToCanvas(x0, y0);
            ImVec2 p1 = worldToCanvas(x1, y1);
            drawList->AddLine(p0, p1, colorCurve, 2.0f);
        }
    }

    ImGui::InvisibleButton("canvas", canvasSize);
    const bool isCanvasHovered = ImGui::IsItemHovered();

    ImGuiID draggedID = ImGui::GetID("##dragged");
    int draggedPointIndex = ImGui::GetStateStorage()->GetInt(draggedID, -1);

    int hoveredPointIndex = -1;
    const float pointRadius = 6.0f;
    const float pointHitRadius = 8.0f;

    for (size_t i = 0; i < points.size(); ++i) {
        ImVec2 pointCanvas = worldToCanvas(points[i].x, points[i].y);
        float dist = std::sqrt((io.MousePos.x - pointCanvas.x) * (io.MousePos.x - pointCanvas.x) +
                               (io.MousePos.y - pointCanvas.y) * (io.MousePos.y - pointCanvas.y));

        if (dist < pointHitRadius && isCanvasHovered) {
            hoveredPointIndex = static_cast<int>(i);
        }
    }

    if (hoveredPointIndex != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        draggedPointIndex = hoveredPointIndex;
        ImGui::GetStateStorage()->SetInt(draggedID, draggedPointIndex);
    }

    if (draggedPointIndex != -1 && draggedPointIndex < static_cast<int>(points.size()) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        glm::vec2 newWorld = canvasToWorld(io.MousePos);
        newWorld.x = std::clamp(newWorld.x, minX, maxX);
        newWorld.y = std::clamp(newWorld.y, minY, maxY);

        if (draggedPointIndex > 0) {
            newWorld.x = std::max(newWorld.x, points[draggedPointIndex - 1].x + 0.01f);
        }
        if (draggedPointIndex < static_cast<int>(points.size()) - 1) {
            newWorld.x = std::min(newWorld.x, points[draggedPointIndex + 1].x - 0.01f);
        }

        points[draggedPointIndex] = newWorld;
        modified = true;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::GetStateStorage()->SetInt(draggedID, -1);
        draggedPointIndex = -1;
    }

    if (isCanvasHovered && hoveredPointIndex != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (points.size() > 2) {
            points.erase(points.begin() + hoveredPointIndex);
            modified = true;
            hoveredPointIndex = -1;
            ImGui::GetStateStorage()->SetInt(draggedID, -1);
        }
    }

    if (isCanvasHovered && hoveredPointIndex == -1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        glm::vec2 newPoint = canvasToWorld(io.MousePos);
        newPoint.x = std::clamp(newPoint.x, minX, maxX);
        newPoint.y = std::clamp(newPoint.y, minY, maxY);

        auto insertPos = std::lower_bound(points.begin(), points.end(), newPoint,
                                          [](const glm::vec2 &a, const glm::vec2 &b) { return a.x < b.x; });
        points.insert(insertPos, newPoint);
        modified = true;
    }

    for (size_t i = 0; i < points.size(); ++i) {
        ImVec2 pointCanvas = worldToCanvas(points[i].x, points[i].y);
        ImU32 pointColor = colorPoint;

        if (static_cast<int>(i) == draggedPointIndex) {
            pointColor = colorPointActive;
        } else if (static_cast<int>(i) == hoveredPointIndex) {
            pointColor = colorPointHover;
        }

        drawList->AddCircleFilled(pointCanvas, pointRadius, pointColor);
        drawList->AddCircle(pointCanvas, pointRadius, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), 12, 1.5f);
    }

    if (isCanvasHovered) {
        glm::vec2 cursorWorld = canvasToWorld(io.MousePos);
        ImGui::BeginTooltip();
        ImGui::Text("X: %.3f, Y: %.3f", cursorWorld.x, cursorWorld.y);
        ImGui::EndTooltip();
    }

    ImGui::PopID();

    return modified;
}
} // namespace Modules