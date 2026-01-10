#include "Gizmo.h"

#include "Logging/TracyProfiler.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Modules::Gizmo {

static constexpr ImU32 COL_X = IM_COL32(0xED, 0x48, 0x5B, 255);        // #ED485B
static constexpr ImU32 COL_Y = IM_COL32(0x86, 0xC9, 0x3F, 255);        // #86C93F
static constexpr ImU32 COL_Z = IM_COL32(0x41, 0x8B, 0xEF, 255);        // #418BEF
static constexpr ImU32 COL_X_HOVER = IM_COL32(0xFF, 0x6B, 0x7A, 255);  // Lighter red
static constexpr ImU32 COL_Y_HOVER = IM_COL32(0xA8, 0xE0, 0x5F, 255);  // Lighter green
static constexpr ImU32 COL_Z_HOVER = IM_COL32(0x6B, 0xA8, 0xFF, 255);  // Lighter blue
static constexpr ImU32 COL_ACTIVE = IM_COL32(255, 220, 64, 255);       // rgb(255, 220, 64)
static constexpr ImU32 COL_PLANE_XY = IM_COL32(0x41, 0x8B, 0xEF, 100); // Blue (Z normal)
static constexpr ImU32 COL_PLANE_XZ = IM_COL32(0x86, 0xC9, 0x3F, 100); // Green (Y normal)
static constexpr ImU32 COL_PLANE_YZ = IM_COL32(0xED, 0x48, 0x5B, 100); // Red (X normal)
static constexpr ImU32 COL_WHITE = IM_COL32(255, 255, 255, 200);
static constexpr ImU32 COL_LABEL_BG = IM_COL32(30, 30, 30, 220);

static constexpr float PI = 3.14159265359f;
static constexpr float TWO_PI = 6.28318530718f;
static constexpr int RING_SEGMENTS = 48;

template <typename T> static T s_clamp(T val, T lo, T hi)
{
    return val < lo ? lo : (val > hi ? hi : val);
}

// Math utilities
static ImVec2 s_worldToScreen(const glm::vec3 &world, const glm::mat4 &viewProj, const ImVec2 &vpPos, const ImVec2 &vpSize)
{
    glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f) {
        return ImVec2(-10000.0f, -10000.0f);
    }
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return ImVec2(vpPos.x + (ndc.x * 0.5f + 0.5f) * vpSize.x, vpPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpSize.y);
}

static void s_screenToWorldRay(const ImVec2 &screen, const glm::mat4 &invViewProj, const ImVec2 &vpPos, const ImVec2 &vpSize,
                               glm::vec3 &origin, glm::vec3 &dir)
{
    float ndcX = ((screen.x - vpPos.x) / vpSize.x) * 2.0f - 1.0f;
    float ndcY = 1.0f - ((screen.y - vpPos.y) / vpSize.y) * 2.0f;

    glm::vec4 nearPt = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPt = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

    nearPt /= nearPt.w;
    farPt /= farPt.w;

    origin = glm::vec3(nearPt);
    dir = glm::normalize(glm::vec3(farPt) - glm::vec3(nearPt));
}

static float s_rayPlaneIntersect(const glm::vec3 &rayOrigin, const glm::vec3 &rayDir, const glm::vec3 &planePoint,
                                 const glm::vec3 &planeNormal)
{
    float denom = glm::dot(planeNormal, rayDir);
    if (std::abs(denom) < 0.0001f) {
        return -1.0f;
    }
    return glm::dot(planePoint - rayOrigin, planeNormal) / denom;
}

static float s_distanceToSegment2D(const ImVec2 &p, const ImVec2 &a, const ImVec2 &b)
{
    ImVec2 ab(b.x - a.x, b.y - a.y);
    ImVec2 ap(p.x - a.x, p.y - a.y);

    float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 0.0001f) {
        return std::sqrt(ap.x * ap.x + ap.y * ap.y);
    }

    float t = s_clamp((ap.x * ab.x + ap.y * ab.y) / lenSq, 0.0f, 1.0f);
    ImVec2 closest(a.x + t * ab.x, a.y + t * ab.y);
    ImVec2 diff(p.x - closest.x, p.y - closest.y);
    return std::sqrt(diff.x * diff.x + diff.y * diff.y);
}

static float s_distanceToPoint2D(const ImVec2 &a, const ImVec2 &b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

static bool s_pointInQuad2D(const ImVec2 &p, const ImVec2 quad[4])
{
    auto sign = [](const ImVec2 &p1, const ImVec2 &p2, const ImVec2 &p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };

    float d1 = sign(p, quad[0], quad[1]);
    float d2 = sign(p, quad[1], quad[2]);
    float d3 = sign(p, quad[2], quad[0]);

    bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    if (!(hasNeg && hasPos)) return true;

    d1 = sign(p, quad[0], quad[2]);
    d2 = sign(p, quad[2], quad[3]);
    d3 = sign(p, quad[3], quad[0]);

    hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(hasNeg && hasPos);
}

static glm::vec3 s_getAxisVector(Axis axis)
{
    switch (axis) {
    case Axis::X:
        return glm::vec3(1, 0, 0);
    case Axis::Y:
        return glm::vec3(0, 1, 0);
    case Axis::Z:
        return glm::vec3(0, 0, 1);
    default:
        return glm::vec3(0);
    }
}

static glm::vec3 s_getPlaneNormal(Axis axis)
{
    switch (axis) {
    case Axis::XY:
        return glm::vec3(0, 0, 1);
    case Axis::XZ:
        return glm::vec3(0, 1, 0);
    case Axis::YZ:
        return glm::vec3(1, 0, 0);
    default:
        return glm::vec3(0, 0, 1);
    }
}

static ImU32 s_getAxisColor(Axis axis, bool hovered, bool active)
{
    if (active) return COL_ACTIVE;
    switch (axis) {
    case Axis::X:
        return hovered ? COL_X_HOVER : COL_X;
    case Axis::Y:
        return hovered ? COL_Y_HOVER : COL_Y;
    case Axis::Z:
        return hovered ? COL_Z_HOVER : COL_Z;
    case Axis::XY:
        return hovered ? COL_Z_HOVER : COL_Z;
    case Axis::XZ:
        return hovered ? COL_Y_HOVER : COL_Y;
    case Axis::YZ:
        return hovered ? COL_X_HOVER : COL_X;
    default:
        return COL_WHITE;
    }
}

static ImU32 s_getPlaneColor(Axis axis, bool hovered, bool active)
{
    if (active) return IM_COL32(255, 220, 64, 150);
    switch (axis) {
    case Axis::XY:
        return hovered ? IM_COL32(0x41, 0x8B, 0xEF, 180) : COL_PLANE_XY;
    case Axis::XZ:
        return hovered ? IM_COL32(0x86, 0xC9, 0x3F, 180) : COL_PLANE_XZ;
    case Axis::YZ:
        return hovered ? IM_COL32(0xED, 0x48, 0x5B, 180) : COL_PLANE_YZ;
    default:
        return COL_WHITE;
    }
}

// Drawing functions
static void s_drawArrow(ImDrawList *dl, const ImVec2 &start, const ImVec2 &end, ImU32 color, float thickness, float arrowSize)
{
    dl->AddLine(start, end, color, thickness);

    ImVec2 dir(end.x - start.x, end.y - start.y);
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001f) return;

    dir.x /= len;
    dir.y /= len;

    ImVec2 perp(-dir.y, dir.x);
    ImVec2 base(end.x - dir.x * arrowSize, end.y - dir.y * arrowSize);
    ImVec2 left(base.x + perp.x * arrowSize * 0.4f, base.y + perp.y * arrowSize * 0.4f);
    ImVec2 right(base.x - perp.x * arrowSize * 0.4f, base.y - perp.y * arrowSize * 0.4f);

    dl->AddTriangleFilled(end, left, right, color);
}

static void s_drawPlaneHandle(ImDrawList *dl, const ImVec2 quad[4], ImU32 color)
{
    dl->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], color);
    ImU32 borderColor = (color & 0x00FFFFFF) | 0xFF000000;
    dl->AddQuad(quad[0], quad[1], quad[2], quad[3], borderColor, 1.5f);
}

static void s_drawScaleHandle(ImDrawList *dl, const ImVec2 &pos, float size, ImU32 color)
{
    dl->AddRectFilled(ImVec2(pos.x - size * 0.5f, pos.y - size * 0.5f), ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), color);
}

// Draw a value label near the mouse
static void s_drawValueLabel(ImDrawList *dl, const ImVec2 &pos, const char *text, ImU32 textColor)
{
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 padding(6, 3);
    ImVec2 labelPos(pos.x + 20, pos.y - 10);

    dl->AddRectFilled(ImVec2(labelPos.x - padding.x, labelPos.y - padding.y),
                      ImVec2(labelPos.x + textSize.x + padding.x, labelPos.y + textSize.y + padding.y), COL_LABEL_BG, 4.0f);
    dl->AddText(labelPos, textColor, text);
}

// Draw a 3D ring around an axis
static void s_draw3DRing(ImDrawList *dl, const glm::vec3 &center, const glm::vec3 &axis, float radius, const glm::mat4 &viewProj,
                         const ImVec2 &vpPos, const ImVec2 &vpSize, const glm::vec3 &cameraPos, ImU32 color, float thickness)
{
    glm::vec3 up = glm::normalize(axis);
    glm::vec3 right;
    if (std::abs(up.y) < 0.99f) {
        right = glm::normalize(glm::cross(up, glm::vec3(0, 1, 0)));
    } else {
        right = glm::normalize(glm::cross(up, glm::vec3(1, 0, 0)));
    }
    glm::vec3 forward = glm::cross(right, up);

    glm::vec3 toCamera = glm::normalize(cameraPos - center);

    ImVec2 prevScreen;
    bool prevVisible = false;
    bool prevBehind = false;

    for (int i = 0; i <= RING_SEGMENTS; i++) {
        float angle = (static_cast<float>(i) / RING_SEGMENTS) * TWO_PI;
        glm::vec3 worldPt = center + (right * std::cos(angle) + forward * std::sin(angle)) * radius;

        glm::vec3 pointNormal = glm::normalize(worldPt - center);
        float dot = glm::dot(pointNormal, toCamera);
        bool isBehind = dot < -0.1f;

        ImVec2 screenPt = s_worldToScreen(worldPt, viewProj, vpPos, vpSize);
        bool isVisible = screenPt.x > -9000.0f;

        if (i > 0 && isVisible && prevVisible) {
            ImU32 segColor = color;
            if (isBehind || prevBehind) {
                segColor = (color & 0x00FFFFFF) | 0x40000000;
            }
            dl->AddLine(prevScreen, screenPt, segColor, thickness);
        }

        prevScreen = screenPt;
        prevVisible = isVisible;
        prevBehind = isBehind;
    }
}

// Draw rotation arc on a 3D ring showing the delta angle
static void s_draw3DRotationArc(ImDrawList *dl, const glm::vec3 &center, const glm::vec3 &axis, float radius, float startAngle,
                                float deltaAngle, const glm::mat4 &viewProj, const ImVec2 &vpPos, const ImVec2 &vpSize,
                                ImU32 fillColor)
{
    if (std::abs(deltaAngle) < 0.001f) return;

    glm::vec3 up = glm::normalize(axis);
    glm::vec3 right;
    if (std::abs(up.y) < 0.99f) {
        right = glm::normalize(glm::cross(up, glm::vec3(0, 1, 0)));
    } else {
        right = glm::normalize(glm::cross(up, glm::vec3(1, 0, 0)));
    }
    glm::vec3 forward = glm::cross(right, up);

    int segments = std::max(8, std::min(32, static_cast<int>(std::abs(deltaAngle) / PI * 24.0f)));

    // Build path for arc fill using fixed-size array
    ImVec2 points[64];
    int pointCount = 0;

    ImVec2 centerScreen = s_worldToScreen(center, viewProj, vpPos, vpSize);
    points[pointCount++] = centerScreen;

    for (int i = 0; i <= segments && pointCount < 64; i++) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float angle = startAngle + t * deltaAngle;
        glm::vec3 worldPt = center + (right * std::cos(angle) + forward * std::sin(angle)) * radius;
        ImVec2 screenPt = s_worldToScreen(worldPt, viewProj, vpPos, vpSize);
        if (screenPt.x > -9000.0f) {
            points[pointCount++] = screenPt;
        }
    }

    if (pointCount > 2) {
        dl->AddConvexPolyFilled(points, pointCount, fillColor);
    }
}

// Distance from mouse to a 3D ring (for hit testing)
static float s_distanceToRing3D(const ImVec2 &mouse, const glm::vec3 &center, const glm::vec3 &axis, float radius,
                                const glm::mat4 &viewProj, const ImVec2 &vpPos, const ImVec2 &vpSize)
{
    glm::vec3 up = glm::normalize(axis);
    glm::vec3 right;
    if (std::abs(up.y) < 0.99f) {
        right = glm::normalize(glm::cross(up, glm::vec3(0, 1, 0)));
    } else {
        right = glm::normalize(glm::cross(up, glm::vec3(1, 0, 0)));
    }
    glm::vec3 forward = glm::cross(right, up);

    float minDist = 1e10f;
    ImVec2 prevScreen;
    bool hasPrev = false;

    constexpr int hitSegments = 24;
    for (int i = 0; i <= hitSegments; i++) {
        float angle = (static_cast<float>(i) / hitSegments) * TWO_PI;
        glm::vec3 worldPt = center + (right * std::cos(angle) + forward * std::sin(angle)) * radius;
        ImVec2 screenPt = s_worldToScreen(worldPt, viewProj, vpPos, vpSize);

        if (screenPt.x > -9000.0f) {
            if (hasPrev && prevScreen.x > -9000.0f) {
                float dist = s_distanceToSegment2D(mouse, prevScreen, screenPt);
                minDist = std::min(minDist, dist);
            }
            prevScreen = screenPt;
            hasPrev = true;
        }
    }

    return minDist;
}

// Get the angle on a 3D ring for rotation
static float s_getAngleOnRing(const ImVec2 &mouse, const glm::vec3 &center, const glm::vec3 &axis, const glm::mat4 &viewProj,
                              const glm::mat4 &invViewProj, const ImVec2 &vpPos, const ImVec2 &vpSize)
{
    // Project center to screen
    ImVec2 centerScreen = s_worldToScreen(center, viewProj, vpPos, vpSize);

    // Use screen-space angle relative to center
    return std::atan2(mouse.y - centerScreen.y, mouse.x - centerScreen.x);
}

// Impl struct
struct Gizmo::Impl {
    enum class State {
        IDLE,
        HOVERING,
        DRAGGING
    };

    State state = State::IDLE;
    Axis hoveredAxis = Axis::NONE;
    Axis activeAxis = Axis::NONE;
    Operation activeOp = Operation::TRANSLATE;

    // Drag state
    glm::vec3 dragStartHitPoint{0.0f};
    float dragStartAngle = 0.0f;
    float dragCurrentAngle = 0.0f; // Track current angle for arc drawing
    float dragStartDistance = 0.0f;
    float accumulatedRotation = 0.0f;       // Total rotation accumulated
    glm::vec3 accumulatedTranslation{0.0f}; // Total translation accumulated
    glm::vec3 accumulatedScale{1.0f};       // Total scale accumulated
    glm::vec3 dragPlaneNormal{0.0f, 1.0f, 0.0f};

    // Previous frame gizmo center
    glm::vec3 lastGizmoCenter{0.0f};
    bool firstFrame = true;

    // Current frame data
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 projMatrix{1.0f};
    glm::mat4 viewProjMatrix{1.0f};
    glm::mat4 invViewProjMatrix{1.0f};
    glm::vec3 gizmoCenter{0.0f};
    glm::vec3 cameraPos{0.0f};
    glm::vec3 cameraDir{0.0f, 0.0f, -1.0f};
    float worldScale = 1.0f;
    ImVec2 viewportPos{0, 0};
    ImVec2 viewportSize{800, 600};

    // Local/World space
    Space currentSpace = Space::WORLD;
    glm::mat3 gizmoOrientation{1.0f}; // Rotation matrix for local space
    glm::vec3 axisX{1, 0, 0};
    glm::vec3 axisY{0, 1, 0};
    glm::vec3 axisZ{0, 0, 1};

    Config config;

    Axis hitTestTranslate(const ImVec2 &mouse);
    Axis hitTestRotate(const ImVec2 &mouse);
    Axis hitTestScale(const ImVec2 &mouse);

    void drawTranslate(ImDrawList *dl, Axis hovered, bool active, const ImVec2 &mouse);
    void drawRotate(ImDrawList *dl, Axis hovered, bool active);
    void drawScale(ImDrawList *dl, Axis hovered, bool active, const ImVec2 &mouse);

    glm::vec3 computeTranslationDelta(const ImVec2 &mouse);
    float computeRotationDelta(const ImVec2 &mouse);
    glm::vec3 computeScaleDelta(const ImVec2 &mouse);

    bool shouldSnap() const;
    float applySnap(float value, float snapSize) const;

    void computeWorldScale();
    void updateOrientation(const glm::mat4 &objectTransform, Space space);
    glm::vec3 getOrientedAxis(Axis axis) const;
    void getPlaneHandleQuad(Axis axis, glm::vec3 corners[4]);
};

void Gizmo::Impl::computeWorldScale()
{
    float distance = glm::length(gizmoCenter - cameraPos);
    float targetScreenPixels = 120.0f * config.sizeFactor / 0.15f;

    float focalLength = projMatrix[1][1];
    if (focalLength > 0.0f) {
        worldScale = (targetScreenPixels / viewportSize.y) * distance * 2.0f / focalLength;
    } else {
        worldScale = distance * 0.15f;
    }

    worldScale = s_clamp(worldScale, 0.01f, 1000.0f);
}

void Gizmo::Impl::updateOrientation(const glm::mat4 &objectTransform, Space space)
{
    currentSpace = space;
    if (space == Space::LOCAL) {
        // Extract rotation from transform (upper-left 3x3, normalized)
        axisX = glm::normalize(glm::vec3(objectTransform[0]));
        axisY = glm::normalize(glm::vec3(objectTransform[1]));
        axisZ = glm::normalize(glm::vec3(objectTransform[2]));
        gizmoOrientation = glm::mat3(axisX, axisY, axisZ);
    } else {
        // World space - use identity
        axisX = glm::vec3(1, 0, 0);
        axisY = glm::vec3(0, 1, 0);
        axisZ = glm::vec3(0, 0, 1);
        gizmoOrientation = glm::mat3(1.0f);
    }
}

glm::vec3 Gizmo::Impl::getOrientedAxis(Axis axis) const
{
    switch (axis) {
    case Axis::X:
        return axisX;
    case Axis::Y:
        return axisY;
    case Axis::Z:
        return axisZ;
    default:
        return glm::vec3(0.0f);
    }
}

void Gizmo::Impl::getPlaneHandleQuad(Axis axis, glm::vec3 corners[4])
{
    float offset = worldScale * 0.3f;
    float size = worldScale * 0.15f;

    glm::vec3 dir1, dir2;
    switch (axis) {
    case Axis::XY:
        dir1 = axisX;
        dir2 = axisY;
        break;
    case Axis::XZ:
        dir1 = axisX;
        dir2 = axisZ;
        break;
    case Axis::YZ:
        dir1 = axisY;
        dir2 = axisZ;
        break;
    default:
        return;
    }

    corners[0] = gizmoCenter + dir1 * offset + dir2 * offset;
    corners[1] = gizmoCenter + dir1 * (offset + size) + dir2 * offset;
    corners[2] = gizmoCenter + dir1 * (offset + size) + dir2 * (offset + size);
    corners[3] = gizmoCenter + dir1 * offset + dir2 * (offset + size);
}

Axis Gizmo::Impl::hitTestTranslate(const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::hitTestTranslate");

    float threshold = config.pickRadius;
    ImVec2 center = s_worldToScreen(gizmoCenter, viewProjMatrix, viewportPos, viewportSize);

    glm::vec3 xEnd = gizmoCenter + axisX * worldScale;
    glm::vec3 yEnd = gizmoCenter + axisY * worldScale;
    glm::vec3 zEnd = gizmoCenter + axisZ * worldScale;

    ImVec2 xScreen = s_worldToScreen(xEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 yScreen = s_worldToScreen(yEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 zScreen = s_worldToScreen(zEnd, viewProjMatrix, viewportPos, viewportSize);

    auto testPlane = [&](Axis axis) -> bool {
        glm::vec3 corners[4];
        getPlaneHandleQuad(axis, corners);
        ImVec2 quad[4];
        for (int i = 0; i < 4; i++) {
            quad[i] = s_worldToScreen(corners[i], viewProjMatrix, viewportPos, viewportSize);
        }
        return s_pointInQuad2D(mouse, quad);
    };

    if (testPlane(Axis::XY)) return Axis::XY;
    if (testPlane(Axis::XZ)) return Axis::XZ;
    if (testPlane(Axis::YZ)) return Axis::YZ;

    float distX = s_distanceToSegment2D(mouse, center, xScreen);
    float distY = s_distanceToSegment2D(mouse, center, yScreen);
    float distZ = s_distanceToSegment2D(mouse, center, zScreen);

    float minDist = threshold;
    Axis result = Axis::NONE;

    if (distX < minDist) {
        minDist = distX;
        result = Axis::X;
    }
    if (distY < minDist) {
        minDist = distY;
        result = Axis::Y;
    }
    if (distZ < minDist) {
        minDist = distZ;
        result = Axis::Z;
    }

    return result;
}

Axis Gizmo::Impl::hitTestRotate(const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::hitTestRotate");

    float threshold = config.pickRadius * 1.5f;
    float ringRadius = worldScale * config.ringRadius;

    float distX = s_distanceToRing3D(mouse, gizmoCenter, axisX, ringRadius, viewProjMatrix, viewportPos, viewportSize);
    float distY = s_distanceToRing3D(mouse, gizmoCenter, axisY, ringRadius, viewProjMatrix, viewportPos, viewportSize);
    float distZ = s_distanceToRing3D(mouse, gizmoCenter, axisZ, ringRadius, viewProjMatrix, viewportPos, viewportSize);

    float minDist = threshold;
    Axis result = Axis::NONE;

    if (distX < minDist) {
        minDist = distX;
        result = Axis::X;
    }
    if (distY < minDist) {
        minDist = distY;
        result = Axis::Y;
    }
    if (distZ < minDist) {
        minDist = distZ;
        result = Axis::Z;
    }

    return result;
}

Axis Gizmo::Impl::hitTestScale(const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::hitTestScale");

    float threshold = config.pickRadius;
    ImVec2 center = s_worldToScreen(gizmoCenter, viewProjMatrix, viewportPos, viewportSize);

    if (s_distanceToPoint2D(mouse, center) < config.handleSize * 1.5f) {
        return Axis::XYZ;
    }

    // Test plane handles first (like translation)
    auto testPlane = [&](Axis axis) -> bool {
        glm::vec3 corners[4];
        getPlaneHandleQuad(axis, corners);
        ImVec2 quad[4];
        for (int i = 0; i < 4; i++) {
            quad[i] = s_worldToScreen(corners[i], viewProjMatrix, viewportPos, viewportSize);
        }
        return s_pointInQuad2D(mouse, quad);
    };

    if (testPlane(Axis::XY)) return Axis::XY;
    if (testPlane(Axis::XZ)) return Axis::XZ;
    if (testPlane(Axis::YZ)) return Axis::YZ;

    glm::vec3 xEnd = gizmoCenter + axisX * worldScale;
    glm::vec3 yEnd = gizmoCenter + axisY * worldScale;
    glm::vec3 zEnd = gizmoCenter + axisZ * worldScale;

    ImVec2 xScreen = s_worldToScreen(xEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 yScreen = s_worldToScreen(yEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 zScreen = s_worldToScreen(zEnd, viewProjMatrix, viewportPos, viewportSize);

    if (s_distanceToPoint2D(mouse, xScreen) < config.handleSize * 1.2f) return Axis::X;
    if (s_distanceToPoint2D(mouse, yScreen) < config.handleSize * 1.2f) return Axis::Y;
    if (s_distanceToPoint2D(mouse, zScreen) < config.handleSize * 1.2f) return Axis::Z;

    float distX = s_distanceToSegment2D(mouse, center, xScreen);
    float distY = s_distanceToSegment2D(mouse, center, yScreen);
    float distZ = s_distanceToSegment2D(mouse, center, zScreen);

    float minDist = threshold;
    Axis result = Axis::NONE;

    if (distX < minDist) {
        minDist = distX;
        result = Axis::X;
    }
    if (distY < minDist) {
        minDist = distY;
        result = Axis::Y;
    }
    if (distZ < minDist) {
        minDist = distZ;
        result = Axis::Z;
    }

    return result;
}

void Gizmo::Impl::drawTranslate(ImDrawList *dl, Axis hovered, bool active, const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::drawTranslate");

    ImVec2 center = s_worldToScreen(gizmoCenter, viewProjMatrix, viewportPos, viewportSize);

    glm::vec3 xEnd = gizmoCenter + axisX * worldScale;
    glm::vec3 yEnd = gizmoCenter + axisY * worldScale;
    glm::vec3 zEnd = gizmoCenter + axisZ * worldScale;

    ImVec2 xScreen = s_worldToScreen(xEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 yScreen = s_worldToScreen(yEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 zScreen = s_worldToScreen(zEnd, viewProjMatrix, viewportPos, viewportSize);

    auto drawPlaneHandle = [&](Axis axis) {
        glm::vec3 corners[4];
        getPlaneHandleQuad(axis, corners);
        ImVec2 quad[4];
        for (int i = 0; i < 4; i++) {
            quad[i] = s_worldToScreen(corners[i], viewProjMatrix, viewportPos, viewportSize);
        }
        bool isHovered = (hovered == axis) || (active && activeAxis == axis);
        ImU32 color = s_getPlaneColor(axis, isHovered, active && activeAxis == axis);
        s_drawPlaneHandle(dl, quad, color);
    };

    drawPlaneHandle(Axis::XY);
    drawPlaneHandle(Axis::XZ);
    drawPlaneHandle(Axis::YZ);

    bool xActive = active && activeAxis == Axis::X;
    bool yActive = active && activeAxis == Axis::Y;
    bool zActive = active && activeAxis == Axis::Z;

    s_drawArrow(dl, center, xScreen, s_getAxisColor(Axis::X, hovered == Axis::X, xActive), config.thickness, config.arrowSize);
    s_drawArrow(dl, center, yScreen, s_getAxisColor(Axis::Y, hovered == Axis::Y, yActive), config.thickness, config.arrowSize);
    s_drawArrow(dl, center, zScreen, s_getAxisColor(Axis::Z, hovered == Axis::Z, zActive), config.thickness, config.arrowSize);

    // Draw value label when dragging
    if (active && activeOp == Operation::TRANSLATE) {
        char text[64];
        glm::vec3 t = accumulatedTranslation;
        if (activeAxis == Axis::X) {
            snprintf(text, sizeof(text), "X: %.2f", t.x);
        } else if (activeAxis == Axis::Y) {
            snprintf(text, sizeof(text), "Y: %.2f", t.y);
        } else if (activeAxis == Axis::Z) {
            snprintf(text, sizeof(text), "Z: %.2f", t.z);
        } else {
            snprintf(text, sizeof(text), "%.2f, %.2f, %.2f", t.x, t.y, t.z);
        }
        s_drawValueLabel(dl, mouse, text, COL_WHITE);
    }
}

void Gizmo::Impl::drawRotate(ImDrawList *dl, Axis hovered, bool active)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::drawRotate");

    float ringRadius = worldScale * config.ringRadius;

    bool xActive = active && activeAxis == Axis::X;
    bool yActive = active && activeAxis == Axis::Y;
    bool zActive = active && activeAxis == Axis::Z;

    if (active) {
        if (activeAxis == Axis::X) {
            s_draw3DRing(dl, gizmoCenter, axisX, ringRadius, viewProjMatrix, viewportPos, viewportSize, cameraPos,
                         s_getAxisColor(Axis::X, hovered == Axis::X, xActive), config.thickness);
        } else if (activeAxis == Axis::Y) {
            s_draw3DRing(dl, gizmoCenter, axisY, ringRadius, viewProjMatrix, viewportPos, viewportSize, cameraPos,
                         s_getAxisColor(Axis::Y, hovered == Axis::Y, yActive), config.thickness);
        } else if (activeAxis == Axis::Z) {
            s_draw3DRing(dl, gizmoCenter, axisZ, ringRadius, viewProjMatrix, viewportPos, viewportSize, cameraPos,
                         s_getAxisColor(Axis::Z, hovered == Axis::Z, zActive), config.thickness);
        }
    } else {
        s_draw3DRing(dl, gizmoCenter, axisX, ringRadius, viewProjMatrix, viewportPos, viewportSize, cameraPos,
                     s_getAxisColor(Axis::X, hovered == Axis::X, xActive), config.thickness);
        s_draw3DRing(dl, gizmoCenter, axisY, ringRadius, viewProjMatrix, viewportPos, viewportSize, cameraPos,
                     s_getAxisColor(Axis::Y, hovered == Axis::Y, yActive), config.thickness);
        s_draw3DRing(dl, gizmoCenter, axisZ, ringRadius, viewProjMatrix, viewportPos, viewportSize, cameraPos,
                     s_getAxisColor(Axis::Z, hovered == Axis::Z, zActive), config.thickness);
    }

    // Draw rotation arc showing the accumulated delta
    if (active && std::abs(accumulatedRotation) > 0.001f) {
        glm::vec3 axis = getOrientedAxis(activeAxis);
        ImU32 arcColor = IM_COL32(255, 220, 64, 120);
        s_draw3DRotationArc(dl, gizmoCenter, axis, ringRadius * 0.9f, dragStartAngle, accumulatedRotation, viewProjMatrix,
                            viewportPos, viewportSize, arcColor);

        // Draw value label
        ImVec2 centerScreen = s_worldToScreen(gizmoCenter, viewProjMatrix, viewportPos, viewportSize);
        float degrees = accumulatedRotation * 180.0f / PI;
        char text[32];
        snprintf(text, sizeof(text), "%.1f°", degrees);

        // Position label at the arc edge
        float labelAngle = dragStartAngle + accumulatedRotation * 0.5f;
        ImVec2 labelPos(centerScreen.x + std::cos(labelAngle) * 80.0f, centerScreen.y + std::sin(labelAngle) * 80.0f);
        s_drawValueLabel(dl, labelPos, text, COL_WHITE);
    }
}

void Gizmo::Impl::drawScale(ImDrawList *dl, Axis hovered, bool active, const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::drawScale");

    ImVec2 center = s_worldToScreen(gizmoCenter, viewProjMatrix, viewportPos, viewportSize);

    glm::vec3 xEnd = gizmoCenter + axisX * worldScale;
    glm::vec3 yEnd = gizmoCenter + axisY * worldScale;
    glm::vec3 zEnd = gizmoCenter + axisZ * worldScale;

    ImVec2 xScreen = s_worldToScreen(xEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 yScreen = s_worldToScreen(yEnd, viewProjMatrix, viewportPos, viewportSize);
    ImVec2 zScreen = s_worldToScreen(zEnd, viewProjMatrix, viewportPos, viewportSize);

    // Draw plane handles (like translation)
    auto drawPlaneHandle = [&](Axis axis) {
        glm::vec3 corners[4];
        getPlaneHandleQuad(axis, corners);
        ImVec2 quad[4];
        for (int i = 0; i < 4; i++) {
            quad[i] = s_worldToScreen(corners[i], viewProjMatrix, viewportPos, viewportSize);
        }
        bool isHovered = (hovered == axis) || (active && activeAxis == axis);
        ImU32 color = s_getPlaneColor(axis, isHovered, active && activeAxis == axis);
        s_drawPlaneHandle(dl, quad, color);
    };

    drawPlaneHandle(Axis::XY);
    drawPlaneHandle(Axis::XZ);
    drawPlaneHandle(Axis::YZ);

    bool xActive = active && activeAxis == Axis::X;
    bool yActive = active && activeAxis == Axis::Y;
    bool zActive = active && activeAxis == Axis::Z;
    bool allActive = active && activeAxis == Axis::XYZ;
    bool xyActive = active && activeAxis == Axis::XY;
    bool xzActive = active && activeAxis == Axis::XZ;
    bool yzActive = active && activeAxis == Axis::YZ;

    dl->AddLine(center, xScreen, s_getAxisColor(Axis::X, hovered == Axis::X, xActive), config.thickness);
    dl->AddLine(center, yScreen, s_getAxisColor(Axis::Y, hovered == Axis::Y, yActive), config.thickness);
    dl->AddLine(center, zScreen, s_getAxisColor(Axis::Z, hovered == Axis::Z, zActive), config.thickness);

    s_drawScaleHandle(dl, xScreen, config.handleSize, s_getAxisColor(Axis::X, hovered == Axis::X, xActive));
    s_drawScaleHandle(dl, yScreen, config.handleSize, s_getAxisColor(Axis::Y, hovered == Axis::Y, yActive));
    s_drawScaleHandle(dl, zScreen, config.handleSize, s_getAxisColor(Axis::Z, hovered == Axis::Z, zActive));

    ImU32 centerCol = (hovered == Axis::XYZ || allActive) ? COL_ACTIVE : COL_WHITE;
    s_drawScaleHandle(dl, center, config.handleSize * 1.2f, centerCol);

    // Draw value label when dragging
    if (active && activeOp == Operation::SCALE) {
        char text[64];
        glm::vec3 s = accumulatedScale;
        if (activeAxis == Axis::X) {
            snprintf(text, sizeof(text), "X: %.2f", s.x);
        } else if (activeAxis == Axis::Y) {
            snprintf(text, sizeof(text), "Y: %.2f", s.y);
        } else if (activeAxis == Axis::Z) {
            snprintf(text, sizeof(text), "Z: %.2f", s.z);
        } else if (activeAxis == Axis::XYZ) {
            snprintf(text, sizeof(text), "%.2f", s.x);
        } else {
            snprintf(text, sizeof(text), "%.2f, %.2f, %.2f", s.x, s.y, s.z);
        }
        s_drawValueLabel(dl, mouse, text, COL_WHITE);
    }
}

glm::vec3 Gizmo::Impl::computeTranslationDelta(const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::computeTranslationDelta");

    glm::vec3 rayOrigin, rayDir;
    s_screenToWorldRay(mouse, invViewProjMatrix, viewportPos, viewportSize, rayOrigin, rayDir);

    float t = s_rayPlaneIntersect(rayOrigin, rayDir, dragStartHitPoint, dragPlaneNormal);
    if (t < 0) return glm::vec3(0.0f);

    glm::vec3 currentHitPoint = rayOrigin + rayDir * t;
    glm::vec3 worldDelta = currentHitPoint - dragStartHitPoint;

    // Constrain delta to the active axis/plane using oriented axes
    glm::vec3 delta(0.0f);
    switch (activeAxis) {
    case Axis::X:
        delta = axisX * glm::dot(worldDelta, axisX);
        break;
    case Axis::Y:
        delta = axisY * glm::dot(worldDelta, axisY);
        break;
    case Axis::Z:
        delta = axisZ * glm::dot(worldDelta, axisZ);
        break;
    case Axis::XY:
        delta = axisX * glm::dot(worldDelta, axisX) + axisY * glm::dot(worldDelta, axisY);
        break;
    case Axis::XZ:
        delta = axisX * glm::dot(worldDelta, axisX) + axisZ * glm::dot(worldDelta, axisZ);
        break;
    case Axis::YZ:
        delta = axisY * glm::dot(worldDelta, axisY) + axisZ * glm::dot(worldDelta, axisZ);
        break;
    default:
        delta = worldDelta;
        break;
    }

    if (shouldSnap()) {
        // Snap along each oriented axis
        float snapX = applySnap(glm::dot(delta, axisX), config.snap.translate);
        float snapY = applySnap(glm::dot(delta, axisY), config.snap.translate);
        float snapZ = applySnap(glm::dot(delta, axisZ), config.snap.translate);
        delta = axisX * snapX + axisY * snapY + axisZ * snapZ;
    }

    dragStartHitPoint = currentHitPoint;
    accumulatedTranslation += delta;

    return delta;
}

float Gizmo::Impl::computeRotationDelta(const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::computeRotationDelta");

    ImVec2 center = s_worldToScreen(gizmoCenter, viewProjMatrix, viewportPos, viewportSize);

    float currentAngle = std::atan2(mouse.y - center.y, mouse.x - center.x);
    float delta = dragCurrentAngle - currentAngle;

    // Normalize delta to [-PI, PI]
    while (delta > PI) delta -= TWO_PI;
    while (delta < -PI) delta += TWO_PI;

    if (shouldSnap()) {
        float snapRad = config.snap.rotate * PI / 180.0f;
        delta = applySnap(delta, snapRad);
    }

    dragCurrentAngle = currentAngle;
    accumulatedRotation -= delta;

    return delta;
}

glm::vec3 Gizmo::Impl::computeScaleDelta(const ImVec2 &mouse)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::computeScaleDelta");

    ImVec2 center = s_worldToScreen(gizmoCenter, viewProjMatrix, viewportPos, viewportSize);

    float currentDist = s_distanceToPoint2D(mouse, center);
    float scaleFactor = (dragStartDistance > 0.001f) ? currentDist / dragStartDistance : 1.0f;
    scaleFactor = s_clamp(scaleFactor, 0.01f, 100.0f);

    if (shouldSnap()) {
        scaleFactor = applySnap(scaleFactor, config.snap.scale);
        if (scaleFactor < 0.01f) scaleFactor = 0.01f;
    }

    dragStartDistance = currentDist;

    glm::vec3 result(1.0f);
    switch (activeAxis) {
    case Axis::X:
        result.x = scaleFactor;
        accumulatedScale.x *= scaleFactor;
        break;
    case Axis::Y:
        result.y = scaleFactor;
        accumulatedScale.y *= scaleFactor;
        break;
    case Axis::Z:
        result.z = scaleFactor;
        accumulatedScale.z *= scaleFactor;
        break;
    case Axis::XY:
        result.x = scaleFactor;
        result.y = scaleFactor;
        accumulatedScale.x *= scaleFactor;
        accumulatedScale.y *= scaleFactor;
        break;
    case Axis::XZ:
        result.x = scaleFactor;
        result.z = scaleFactor;
        accumulatedScale.x *= scaleFactor;
        accumulatedScale.z *= scaleFactor;
        break;
    case Axis::YZ:
        result.y = scaleFactor;
        result.z = scaleFactor;
        accumulatedScale.y *= scaleFactor;
        accumulatedScale.z *= scaleFactor;
        break;
    case Axis::XYZ:
        result = glm::vec3(scaleFactor);
        accumulatedScale *= scaleFactor;
        break;
    default:
        break;
    }

    return result;
}

bool Gizmo::Impl::shouldSnap() const
{
    ImGuiIO &io = ImGui::GetIO();
    bool shiftHeld = io.KeyShift;

    if (config.snap.shiftToSnap) {
        return shiftHeld || config.snap.enabled;
    } else {
        return config.snap.enabled && !shiftHeld;
    }
}

float Gizmo::Impl::applySnap(float value, float snapSize) const
{
    return std::round(value / snapSize) * snapSize;
}

// Gizmo class implementation
Gizmo::Gizmo() : m_impl(std::make_unique<Impl>()) {}
Gizmo::~Gizmo() = default;
Gizmo::Gizmo(Gizmo &&) noexcept = default;
Gizmo &Gizmo::operator=(Gizmo &&) noexcept = default;

void Gizmo::reset()
{
    m_impl->state = Impl::State::IDLE;
    m_impl->hoveredAxis = Axis::NONE;
    m_impl->activeAxis = Axis::NONE;
    m_impl->firstFrame = true;
    m_impl->accumulatedRotation = 0.0f;
    m_impl->accumulatedTranslation = glm::vec3(0.0f);
    m_impl->accumulatedScale = glm::vec3(1.0f);
}

Config &Gizmo::config()
{
    return m_impl->config;
}
const Config &Gizmo::config() const
{
    return m_impl->config;
}

Result Gizmo::update(const glm::mat4 &view, const glm::mat4 &projection, const glm::mat4 &objectTransform, const glm::vec3 &pivot,
                     Operation op, Space space, ImDrawList *drawList, ImVec2 viewportPos, ImVec2 viewportSize)
{
    RAPTURE_PROFILE_SCOPE("Gizmo::update");

    Result result;
    result.operation = op;

    m_impl->viewMatrix = view;
    m_impl->projMatrix = projection;
    m_impl->viewProjMatrix = projection * view;
    m_impl->invViewProjMatrix = glm::inverse(m_impl->viewProjMatrix);
    m_impl->viewportPos = viewportPos;
    m_impl->viewportSize = viewportSize;

    m_impl->gizmoCenter = glm::vec3(objectTransform * glm::vec4(pivot, 1.0f));

    // Update gizmo orientation based on local/world space
    m_impl->updateOrientation(objectTransform, space);

    float centerDist = glm::length(m_impl->gizmoCenter - m_impl->lastGizmoCenter);
    if (m_impl->firstFrame || centerDist > 0.001f) {
        if (m_impl->state != Impl::State::DRAGGING) {
            m_impl->state = Impl::State::IDLE;
            m_impl->hoveredAxis = Axis::NONE;
            m_impl->activeAxis = Axis::NONE;
        }
        m_impl->lastGizmoCenter = m_impl->gizmoCenter;
        m_impl->firstFrame = false;
    }

    glm::mat4 invView = glm::inverse(view);
    m_impl->cameraPos = glm::vec3(invView[3]);
    m_impl->cameraDir = -glm::normalize(glm::vec3(invView[2]));

    m_impl->computeWorldScale();

    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    bool inViewport = mouse.x >= viewportPos.x && mouse.x <= viewportPos.x + viewportSize.x && mouse.y >= viewportPos.y &&
                      mouse.y <= viewportPos.y + viewportSize.y;

    switch (m_impl->state) {
    case Impl::State::IDLE:
        if (inViewport) {
            switch (op) {
            case Operation::TRANSLATE:
                m_impl->hoveredAxis = m_impl->hitTestTranslate(mouse);
                break;
            case Operation::ROTATE:
                m_impl->hoveredAxis = m_impl->hitTestRotate(mouse);
                break;
            case Operation::SCALE:
                m_impl->hoveredAxis = m_impl->hitTestScale(mouse);
                break;
            case Operation::COMBINED:
                m_impl->hoveredAxis = m_impl->hitTestTranslate(mouse);
                if (m_impl->hoveredAxis == Axis::NONE) m_impl->hoveredAxis = m_impl->hitTestScale(mouse);
                if (m_impl->hoveredAxis == Axis::NONE) m_impl->hoveredAxis = m_impl->hitTestRotate(mouse);
                break;
            }

            if (m_impl->hoveredAxis != Axis::NONE) {
                m_impl->state = Impl::State::HOVERING;
            }
        }
        break;

    case Impl::State::HOVERING:
        switch (op) {
        case Operation::TRANSLATE:
            m_impl->hoveredAxis = m_impl->hitTestTranslate(mouse);
            break;
        case Operation::ROTATE:
            m_impl->hoveredAxis = m_impl->hitTestRotate(mouse);
            break;
        case Operation::SCALE:
            m_impl->hoveredAxis = m_impl->hitTestScale(mouse);
            break;
        case Operation::COMBINED:
            m_impl->hoveredAxis = m_impl->hitTestTranslate(mouse);
            if (m_impl->hoveredAxis == Axis::NONE) m_impl->hoveredAxis = m_impl->hitTestScale(mouse);
            if (m_impl->hoveredAxis == Axis::NONE) m_impl->hoveredAxis = m_impl->hitTestRotate(mouse);
            break;
        }

        if (m_impl->hoveredAxis == Axis::NONE) {
            m_impl->state = Impl::State::IDLE;
        } else if (mouseClicked) {
            m_impl->state = Impl::State::DRAGGING;
            m_impl->activeAxis = m_impl->hoveredAxis;
            m_impl->activeOp = op;

            // Reset accumulated values
            m_impl->accumulatedRotation = 0.0f;
            m_impl->accumulatedTranslation = glm::vec3(0.0f);
            m_impl->accumulatedScale = glm::vec3(1.0f);

            if (op == Operation::TRANSLATE || op == Operation::SCALE || op == Operation::COMBINED) {
                if (m_impl->activeAxis == Axis::XY) {
                    m_impl->dragPlaneNormal = m_impl->axisZ;
                } else if (m_impl->activeAxis == Axis::XZ) {
                    m_impl->dragPlaneNormal = m_impl->axisY;
                } else if (m_impl->activeAxis == Axis::YZ) {
                    m_impl->dragPlaneNormal = m_impl->axisX;
                } else {
                    glm::vec3 axisDir = m_impl->getOrientedAxis(m_impl->activeAxis);
                    glm::vec3 toCamera = glm::normalize(m_impl->cameraPos - m_impl->gizmoCenter);
                    glm::vec3 perp = glm::cross(axisDir, toCamera);
                    if (glm::length(perp) > 0.001f) {
                        m_impl->dragPlaneNormal = glm::normalize(glm::cross(axisDir, perp));
                    } else {
                        m_impl->dragPlaneNormal = m_impl->axisY;
                    }
                }
            }

            glm::vec3 rayOrigin, rayDir;
            s_screenToWorldRay(mouse, m_impl->invViewProjMatrix, viewportPos, viewportSize, rayOrigin, rayDir);
            float t = s_rayPlaneIntersect(rayOrigin, rayDir, m_impl->gizmoCenter, m_impl->dragPlaneNormal);
            m_impl->dragStartHitPoint = (t > 0) ? rayOrigin + rayDir * t : m_impl->gizmoCenter;

            ImVec2 center = s_worldToScreen(m_impl->gizmoCenter, m_impl->viewProjMatrix, viewportPos, viewportSize);
            m_impl->dragStartAngle = std::atan2(mouse.y - center.y, mouse.x - center.x);
            m_impl->dragCurrentAngle = m_impl->dragStartAngle;
            m_impl->dragStartDistance = s_distanceToPoint2D(mouse, center);
        }
        break;

    case Impl::State::DRAGGING:
        if (mouseReleased) {
            m_impl->state = Impl::State::IDLE;
            m_impl->activeAxis = Axis::NONE;
        } else {
            result.active = true;
            result.axis = m_impl->activeAxis;

            switch (m_impl->activeOp) {
            case Operation::TRANSLATE:
            case Operation::COMBINED:
                result.deltaPosition = m_impl->computeTranslationDelta(mouse);
                break;
            case Operation::ROTATE: {
                float rotDelta = m_impl->computeRotationDelta(mouse);
                result.rotationDegrees = rotDelta * 180.0f / PI;
                result.deltaRotation = s_getAxisVector(m_impl->activeAxis) * rotDelta;
            } break;
            case Operation::SCALE:
                result.deltaScale = m_impl->computeScaleDelta(mouse);
                break;
            }
        }
        break;
    }

    result.hovered = (m_impl->state == Impl::State::HOVERING || m_impl->state == Impl::State::DRAGGING);
    if (!result.active) {
        result.axis = m_impl->hoveredAxis;
    }

    if (drawList) {
        RAPTURE_PROFILE_SCOPE("Gizmo::draw");

        drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y), true);

        bool isActive = (m_impl->state == Impl::State::DRAGGING);

        switch (op) {
        case Operation::TRANSLATE:
            m_impl->drawTranslate(drawList, m_impl->hoveredAxis, isActive, mouse);
            break;
        case Operation::ROTATE:
            m_impl->drawRotate(drawList, m_impl->hoveredAxis, isActive);
            break;
        case Operation::SCALE:
            m_impl->drawScale(drawList, m_impl->hoveredAxis, isActive, mouse);
            break;
        case Operation::COMBINED:
            m_impl->drawTranslate(drawList, m_impl->hoveredAxis, isActive, mouse);
            m_impl->drawRotate(drawList, m_impl->hoveredAxis, isActive);
            m_impl->drawScale(drawList, m_impl->hoveredAxis, isActive, mouse);
            break;
        }

        drawList->PopClipRect();
    }

    return result;
}

void Gizmo::renderSettings()
{
    if (ImGui::BeginPopup("GizmoSettings")) {
        ImGui::Text("Gizmo Settings");
        ImGui::Separator();

        ImGui::Checkbox("Snapping", &m_impl->config.snap.enabled);

        if (m_impl->config.snap.enabled) {
            ImGui::Indent();
            ImGui::DragFloat("Translate", &m_impl->config.snap.translate, 0.1f, 0.01f, 100.0f, "%.2f");
            ImGui::DragFloat("Rotate (deg)", &m_impl->config.snap.rotate, 1.0f, 1.0f, 90.0f, "%.0f");
            ImGui::DragFloat("Scale", &m_impl->config.snap.scale, 0.01f, 0.01f, 1.0f, "%.2f");
            ImGui::Unindent();
        }

        ImGui::Separator();

        const char *modText = m_impl->config.snap.shiftToSnap ? "Shift: Enable snap" : "Shift: Disable snap";
        ImGui::TextDisabled("%s", modText);
        if (ImGui::Button("Toggle Modifier")) {
            m_impl->config.snap.shiftToSnap = !m_impl->config.snap.shiftToSnap;
        }

        ImGui::Separator();
        ImGui::Text("Appearance");
        ImGui::DragFloat("Size", &m_impl->config.sizeFactor, 0.01f, 0.05f, 0.5f, "%.2f");
        ImGui::DragFloat("Thickness", &m_impl->config.thickness, 0.5f, 1.0f, 10.0f, "%.1f");

        ImGui::EndPopup();
    }
}

} // namespace Modules::Gizmo
