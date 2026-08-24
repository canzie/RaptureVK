#include "TransformGizmo.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gizmo {

static constexpr float SIZE_FACTOR = 0.25f;
static constexpr float AXIS_LENGTH = 1.0f;
static constexpr float LINE_THICKNESS = 3.0f;
static constexpr float PLANE_OUTLINE_THICKNESS = 1.5f;
static constexpr float PICK_RADIUS = 10.0f;
static constexpr float CONE_LENGTH = 0.25f;
static constexpr float CONE_RADIUS = 0.06f;
static constexpr uint32_t CONE_SEGMENTS = 16;
static constexpr float PLANE_OFFSET = 0.35f;
static constexpr float PLANE_SIZE = 0.15f;
static constexpr float RING_RADIUS = 0.9f;
static constexpr uint32_t RING_SEGMENTS = 48;

// How near to face on a ring has to be before the far half stops being hidden
static constexpr float RING_FACING_TOLERANCE = 0.02f;
static constexpr float SCALE_HANDLE_SIZE = 0.07f;
static constexpr float VALUE_LABEL_FONT_SIZE = 12.0f;
static const glm::vec2 VALUE_LABEL_OFFSET{20.0f, -10.0f};
static const Amethyst::Color4 COL_VALUE_LABEL{1.0f, 1.0f, 1.0f, 0.784f};

// Y is up, so it takes the blue that a Z up editor would give its own vertical
static const glm::vec4 COL_AXIS_X{0.549f, 0.071f, 0.110f, 1.0f};
static const glm::vec4 COL_AXIS_Y{0.078f, 0.220f, 0.600f, 1.0f};
static const glm::vec4 COL_AXIS_Z{0.129f, 0.435f, 0.110f, 1.0f};
static const glm::vec4 COL_AXIS_X_HOVERED{0.722f, 0.157f, 0.220f, 1.0f};
static const glm::vec4 COL_AXIS_Y_HOVERED{0.157f, 0.353f, 0.780f, 1.0f};
static const glm::vec4 COL_AXIS_Z_HOVERED{0.220f, 0.600f, 0.196f, 1.0f};

// A plane handle takes the colour of the axis it faces along
static const glm::vec4 COL_PLANE_XY{0.129f, 0.435f, 0.110f, 0.392f};
static const glm::vec4 COL_PLANE_XZ{0.078f, 0.220f, 0.600f, 0.392f};
static const glm::vec4 COL_PLANE_YZ{0.549f, 0.071f, 0.110f, 0.392f};
static const glm::vec4 COL_PLANE_XY_HOVERED{0.220f, 0.600f, 0.196f, 0.706f};
static const glm::vec4 COL_PLANE_XZ_HOVERED{0.157f, 0.353f, 0.780f, 0.706f};
static const glm::vec4 COL_PLANE_YZ_HOVERED{0.722f, 0.157f, 0.220f, 0.706f};

static const glm::vec4 COL_NEUTRAL{0.549f, 0.549f, 0.549f, 0.784f};
static const glm::vec4 COL_ACTIVE{0.722f, 0.549f, 0.078f, 1.0f};
static const glm::vec4 COL_ACTIVE_FILL{0.722f, 0.549f, 0.078f, 0.588f};

// The two basis axes each plane handle spans, and the ring each rotation handle turns about
static constexpr int PLANE_BASIS[3][2] = {{0, 1}, {0, 2}, {1, 2}};

static float s_distanceToSegment(const glm::vec2 &point, const glm::vec2 &start, const glm::vec2 &end)
{
    const glm::vec2 span = end - start;
    const float lengthSq = glm::dot(span, span);
    if (lengthSq <= 0.0f) {
        return glm::distance(point, start);
    }

    const float t = glm::clamp(glm::dot(point - start, span) / lengthSq, 0.0f, 1.0f);
    return glm::distance(point, start + span * t);
}

static bool s_pointInTriangle(const glm::vec2 &point, const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c)
{
    const float d1 = (point.x - b.x) * (a.y - b.y) - (a.x - b.x) * (point.y - b.y);
    const float d2 = (point.x - c.x) * (b.y - c.y) - (b.x - c.x) * (point.y - c.y);
    const float d3 = (point.x - a.x) * (c.y - a.y) - (c.x - a.x) * (point.y - a.y);

    const bool hasNegative = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool hasPositive = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;

    return !(hasNegative && hasPositive);
}

static bool s_intersectRayPlane(const glm::vec3 &origin, const glm::vec3 &direction, const glm::vec3 &planePoint,
                                const glm::vec3 &planeNormal, glm::vec3 &hit)
{
    const float denominator = glm::dot(direction, planeNormal);
    if (std::abs(denominator) < 1e-6f) {
        return false;
    }

    const float t = glm::dot(planePoint - origin, planeNormal) / denominator;
    if (t < 0.0f) {
        return false;
    }

    hit = origin + direction * t;
    return true;
}

static float s_snapped(float value, float increment)
{
    if (increment <= 0.0f) {
        return value;
    }

    return std::round(value / increment) * increment;
}

/**
 * @brief Whether a drag quantises this frame
 * @param snap The increments asked for
 * @param shiftHeld Whether the modifier is down
 * @return True when the drag should be quantised
 */
static bool s_snapWanted(const TransformGizmo::Snap &snap, bool shiftHeld)
{
    return snap.enabled || (snap.whileShiftHeld && shiftHeld);
}

static glm::vec4 s_axisColor(TransformGizmo::Axis axis, bool hovered, bool active)
{
    if (active) {
        return COL_ACTIVE;
    }

    switch (axis) {
    case TransformGizmo::AXIS_X:
        return hovered ? COL_AXIS_X_HOVERED : COL_AXIS_X;
    case TransformGizmo::AXIS_Y:
        return hovered ? COL_AXIS_Y_HOVERED : COL_AXIS_Y;
    case TransformGizmo::AXIS_Z:
        return hovered ? COL_AXIS_Z_HOVERED : COL_AXIS_Z;
    default:
        return hovered ? COL_ACTIVE : COL_NEUTRAL;
    }
}

/**
 * @brief The fill of a plane handle, which takes the colour of the axis it faces along
 * @param axis The plane
 * @param hovered Whether the cursor is over it
 * @param active Whether it is being dragged
 * @return The fill colour
 */
static glm::vec4 s_planeFill(TransformGizmo::Axis axis, bool hovered, bool active)
{
    if (active) {
        return COL_ACTIVE_FILL;
    }

    switch (axis) {
    case TransformGizmo::AXIS_XY:
        return hovered ? COL_PLANE_XY_HOVERED : COL_PLANE_XY;
    case TransformGizmo::AXIS_XZ:
        return hovered ? COL_PLANE_XZ_HOVERED : COL_PLANE_XZ;
    case TransformGizmo::AXIS_YZ:
        return hovered ? COL_PLANE_YZ_HOVERED : COL_PLANE_YZ;
    default:
        return COL_NEUTRAL;
    }
}

TransformGizmo::TransformGizmo(Amethyst::Container *container)
    : m_handleBatch(Rapture::DEPTH_MODE_ALWAYS_IN_FRONT, Rapture::GIZMO_SHADING_MODE_SOLID)
{
    m_valueLabel = container->add<Amethyst::TextLabel>();
    m_valueLabel->setBaseProperties({.visible = false});
    m_valueLabel->setTextStyleProperties({.fontSize = VALUE_LABEL_FONT_SIZE, .textColor = COL_VALUE_LABEL});
}

void TransformGizmo::updateValueLabel(const Params &params)
{
    if (m_activeAxis == AXIS_NONE) {
        m_valueLabel->setBaseProperties({.visible = false});
        return;
    }

    char text[48];

    switch (m_activeOperation) {
    case OPERATION_TRANSLATE:
    case OPERATION_SCALE: {
        const glm::vec3 value = m_activeOperation == OPERATION_TRANSLATE ? m_appliedTranslate : m_appliedScale;

        if (m_activeAxis >= AXIS_X && m_activeAxis <= AXIS_Z) {
            const char label[3] = {'X', 'Y', 'Z'};
            std::snprintf(text, sizeof(text), "%c: %.2f", label[m_activeAxis - AXIS_X], value[m_activeAxis - AXIS_X]);
        } else {
            std::snprintf(text, sizeof(text), "%.2f, %.2f, %.2f", value.x, value.y, value.z);
        }
        break;
    }
    case OPERATION_ROTATE:
        std::snprintf(text, sizeof(text), "%.1f deg", glm::degrees(m_appliedRotation));
        break;
    case OPERATION_COUNT:
        return;
    }

    const glm::vec2 position = params.cursor + VALUE_LABEL_OFFSET;

    m_valueLabel->setText(text);
    m_valueLabel->setBaseProperties(
        {.position = Amethyst::UDim2::fromOffset(position.x, position.y), .visible = true});
}

glm::vec2 TransformGizmo::toScreen(const glm::vec3 &world) const
{
    glm::vec4 clip = m_viewProj * glm::vec4(world, 1.0f);
    clip.w = std::max(clip.w, 1e-6f);

    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return glm::vec2((ndc.x * 0.5f + 0.5f) * m_viewportSize.x, (ndc.y * 0.5f + 0.5f) * m_viewportSize.y);
}

void TransformGizmo::screenRay(const glm::vec2 &screen, glm::vec3 &origin, glm::vec3 &direction) const
{
    const float ndcX = (screen.x / m_viewportSize.x) * 2.0f - 1.0f;
    const float ndcY = (screen.y / m_viewportSize.y) * 2.0f - 1.0f;

    glm::vec4 nearPoint = m_invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = m_invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    origin = glm::vec3(nearPoint);
    direction = glm::normalize(glm::vec3(farPoint - nearPoint));
}

void TransformGizmo::reset()
{
    m_hoveredAxis = AXIS_NONE;
    m_activeAxis = AXIS_NONE;
    m_appliedTranslate = glm::vec3(0.0f);
    m_appliedScale = glm::vec3(1.0f);
    m_appliedRotation = 0.0f;

    m_valueLabel->setBaseProperties({.visible = false});
}

void TransformGizmo::buildAxes()
{
    m_axes.clear();

    for (int index = 0; index < 3; ++index) {
        AxisHandle handle;
        handle.axis = static_cast<Axis>(AXIS_X + index);
        handle.direction = m_basis[index];
        handle.tip = m_center + m_basis[index] * (m_worldScale * AXIS_LENGTH);
        handle.tipScreen = toScreen(handle.tip);
        m_axes.push_back(handle);
    }
}

void TransformGizmo::buildPlanes()
{
    m_planes.clear();

    const float offset = m_worldScale * PLANE_OFFSET;
    const float size = m_worldScale * PLANE_SIZE;

    for (int index = 0; index < 3; ++index) {
        const glm::vec3 &first = m_basis[PLANE_BASIS[index][0]];
        const glm::vec3 &second = m_basis[PLANE_BASIS[index][1]];

        PlaneHandle handle;
        handle.axis = static_cast<Axis>(AXIS_XY + index);
        handle.corners[0] = m_center + first * offset + second * offset;
        handle.corners[1] = handle.corners[0] + first * size;
        handle.corners[2] = handle.corners[0] + first * size + second * size;
        handle.corners[3] = handle.corners[0] + second * size;

        for (int corner = 0; corner < 4; ++corner) {
            handle.cornersScreen[corner] = toScreen(handle.corners[corner]);
        }

        m_planes.push_back(handle);
    }
}

void TransformGizmo::buildRings()
{
    m_rings.clear();

    const float radius = m_worldScale * RING_RADIUS;
    const float step = glm::two_pi<float>() / static_cast<float>(RING_SEGMENTS);

    for (int index = 0; index < 3; ++index) {
        const glm::vec3 &tangent = m_basis[(index + 1) % 3];
        const glm::vec3 &bitangent = m_basis[(index + 2) % 3];

        RingHandle handle;
        handle.axis = static_cast<Axis>(AXIS_X + index);
        handle.normal = m_basis[index];
        handle.points.reserve(RING_SEGMENTS);
        handle.pointsScreen.reserve(RING_SEGMENTS);
        handle.pointsFacing.reserve(RING_SEGMENTS);

        for (uint32_t segment = 0; segment < RING_SEGMENTS; ++segment) {
            const float angle = step * static_cast<float>(segment);
            const glm::vec3 offset = (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;

            handle.points.push_back(m_center + offset);
            handle.pointsScreen.push_back(toScreen(m_center + offset));

            // A ring seen edge on keeps the half nearer the camera. One seen face on has every point
            // the same distance away, which the tolerance turns into the whole circle
            const float facing = glm::dot(offset / radius, m_toCamera);
            handle.pointsFacing.push_back(facing >= -RING_FACING_TOLERANCE ? 1u : 0u);
        }

        m_rings.push_back(std::move(handle));
    }
}

void TransformGizmo::buildHandles(const Params &params)
{
    m_center = glm::vec3(params.objectTransform * glm::vec4(params.pivot, 1.0f));

    if (params.space == SPACE_LOCAL) {
        for (int index = 0; index < 3; ++index) {
            const glm::vec3 column = glm::vec3(params.objectTransform[index]);
            const float length = glm::length(column);

            if (length > 0.0f) {
                m_basis[index] = column / length;
            } else {
                m_basis[index] = glm::vec3(0.0f);
                m_basis[index][index] = 1.0f;
            }
        }
    } else {
        m_basis[0] = glm::vec3(1.0f, 0.0f, 0.0f);
        m_basis[1] = glm::vec3(0.0f, 1.0f, 0.0f);
        m_basis[2] = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // Clip w is view depth under a perspective projection and one under an orthographic, so the
    // gizmo holds its screen size in both without asking which is in use
    const float clipW = (m_viewProj * glm::vec4(m_center, 1.0f)).w;
    m_worldScale = std::max(std::abs(clipW), 1e-3f) * SIZE_FACTOR;

    const glm::vec3 cameraPosition = glm::vec3(glm::inverse(params.view)[3]);
    const glm::vec3 toCamera = cameraPosition - m_center;
    m_toCamera = glm::length(toCamera) > 1e-5f ? glm::normalize(toCamera) : glm::vec3(0.0f, 0.0f, 1.0f);

    buildAxes();

    if (params.operation == OPERATION_ROTATE) {
        m_planes.clear();
    } else {
        buildPlanes();
    }

    if (params.operation == OPERATION_ROTATE) {
        buildRings();
    } else {
        m_rings.clear();
    }
}

TransformGizmo::Axis TransformGizmo::hitTest(const Params &params) const
{
    if (!params.cursorInside) {
        return AXIS_NONE;
    }

    const glm::vec2 cursor = params.cursor;
    const glm::vec2 centerScreen = toScreen(m_center);

    Axis best = AXIS_NONE;
    float bestDistance = PICK_RADIUS;

    for (const PlaneHandle &plane : m_planes) {
        const bool inside = s_pointInTriangle(cursor, plane.cornersScreen[0], plane.cornersScreen[1], plane.cornersScreen[2]) ||
                            s_pointInTriangle(cursor, plane.cornersScreen[0], plane.cornersScreen[2], plane.cornersScreen[3]);

        if (inside) {
            bestDistance = 0.0f;
            best = plane.axis;
        }
    }

    if (params.operation != OPERATION_ROTATE) {
        for (const AxisHandle &axis : m_axes) {
            const float distance = s_distanceToSegment(cursor, centerScreen, axis.tipScreen);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = axis.axis;
            }
        }
    }

    for (const RingHandle &ring : m_rings) {
        const size_t count = ring.pointsScreen.size();

        for (size_t point = 0; point < count; ++point) {
            const size_t next = (point + 1) % count;

            // Only the drawn half is grabbable, so the hidden back of a ring cannot be caught
            if (ring.pointsFacing[point] == 0u || ring.pointsFacing[next] == 0u) {
                continue;
            }

            const float distance = s_distanceToSegment(cursor, ring.pointsScreen[point], ring.pointsScreen[next]);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = ring.axis;
            }
        }
    }

    return best;
}

void TransformGizmo::beginDrag(const Params &params)
{
    m_activeAxis = m_hoveredAxis;
    m_activeOperation = params.operation;
    m_appliedTranslate = glm::vec3(0.0f);
    m_appliedScale = glm::vec3(1.0f);
    m_appliedRotation = 0.0f;

    m_dragCenter = m_center;
    m_dragBasis[0] = m_basis[0];
    m_dragBasis[1] = m_basis[1];
    m_dragBasis[2] = m_basis[2];

    glm::vec3 rayOrigin;
    glm::vec3 rayDirection;
    screenRay(params.cursor, rayOrigin, rayDirection);

    switch (m_activeAxis) {
    case AXIS_X:
    case AXIS_Y:
    case AXIS_Z: {
        const glm::vec3 direction = m_basis[m_activeAxis - AXIS_X];
        m_dragConstraintDirection = direction;

        // The plane holding the axis that most faces the camera, so the cursor tracks the axis
        // rather than skating along a plane it is nearly parallel to
        const glm::vec3 perpendicular = rayDirection - direction * glm::dot(rayDirection, direction);
        m_dragConstraintNormal = glm::length(perpendicular) > 1e-5f ? glm::normalize(perpendicular) : m_basis[0];
        break;
    }
    case AXIS_XY:
    case AXIS_XZ:
    case AXIS_YZ: {
        const int index = m_activeAxis - AXIS_XY;
        m_dragConstraintNormal = glm::normalize(glm::cross(m_basis[PLANE_BASIS[index][0]], m_basis[PLANE_BASIS[index][1]]));
        m_dragConstraintDirection = glm::vec3(0.0f);
        break;
    }
    default:
        m_dragConstraintNormal = -glm::normalize(rayDirection);
        m_dragConstraintDirection = glm::vec3(0.0f);
        break;
    }

    if (m_activeOperation == OPERATION_ROTATE) {
        const int index = m_activeAxis - AXIS_X;
        m_dragConstraintNormal = m_basis[index];

        glm::vec3 hit;
        if (!s_intersectRayPlane(rayOrigin, rayDirection, m_center, m_dragConstraintNormal, hit)) {
            m_activeAxis = AXIS_NONE;
            return;
        }

        const glm::vec3 offset = hit - m_center;
        m_dragStartAngle = std::atan2(glm::dot(offset, m_basis[(index + 2) % 3]), glm::dot(offset, m_basis[(index + 1) % 3]));
        return;
    }

    if (m_activeOperation == OPERATION_SCALE) {
        m_dragStartDistance = std::max(glm::distance(params.cursor, toScreen(m_center)), 1.0f);
        return;
    }

    if (!s_intersectRayPlane(rayOrigin, rayDirection, m_center, m_dragConstraintNormal, m_dragStartHit)) {
        m_activeAxis = AXIS_NONE;
    }
}

glm::vec3 TransformGizmo::solveTranslate(const Params &params) const
{
    glm::vec3 rayOrigin;
    glm::vec3 rayDirection;
    screenRay(params.cursor, rayOrigin, rayDirection);

    glm::vec3 hit;
    if (!s_intersectRayPlane(rayOrigin, rayDirection, m_dragCenter, m_dragConstraintNormal, hit)) {
        return m_appliedTranslate;
    }

    glm::vec3 offset = hit - m_dragStartHit;

    if (m_activeAxis >= AXIS_X && m_activeAxis <= AXIS_Z) {
        offset = m_dragConstraintDirection * glm::dot(offset, m_dragConstraintDirection);
    }

    if (s_snapWanted(params.snap, params.shiftHeld)) {
        const float length = glm::length(offset);
        if (length > 0.0f) {
            offset *= s_snapped(length, params.snap.translate) / length;
        }
    }

    return offset;
}

float TransformGizmo::solveRotate(const Params &params)
{
    const int index = m_activeAxis - AXIS_X;

    glm::vec3 rayOrigin;
    glm::vec3 rayDirection;
    screenRay(params.cursor, rayOrigin, rayDirection);

    glm::vec3 hit;
    if (!s_intersectRayPlane(rayOrigin, rayDirection, m_dragCenter, m_dragConstraintNormal, hit)) {
        return m_appliedRotation;
    }

    const glm::vec3 offset = hit - m_dragCenter;
    const float angle =
        std::atan2(glm::dot(offset, m_dragBasis[(index + 2) % 3]), glm::dot(offset, m_dragBasis[(index + 1) % 3]));

    float total = angle - m_dragStartAngle;
    while (total - m_appliedRotation > glm::pi<float>()) {
        total -= glm::two_pi<float>();
    }
    while (total - m_appliedRotation < -glm::pi<float>()) {
        total += glm::two_pi<float>();
    }

    if (s_snapWanted(params.snap, params.shiftHeld)) {
        total = glm::radians(s_snapped(glm::degrees(total), params.snap.rotateDegrees));
    }

    return total;
}

glm::vec3 TransformGizmo::solveScale(const Params &params) const
{
    const float distance = std::max(glm::distance(params.cursor, toScreen(m_dragCenter)), 1.0f);
    float factor = distance / m_dragStartDistance;

    if (s_snapWanted(params.snap, params.shiftHeld)) {
        factor = s_snapped(factor, params.snap.scale);
    }

    factor = std::max(factor, 0.01f);

    glm::vec3 scale(1.0f);

    if (m_activeAxis >= AXIS_XY && m_activeAxis <= AXIS_YZ) {
        const int index = m_activeAxis - AXIS_XY;
        scale[PLANE_BASIS[index][0]] = factor;
        scale[PLANE_BASIS[index][1]] = factor;
        return scale;
    }

    scale[m_activeAxis - AXIS_X] = factor;
    return scale;
}

void TransformGizmo::submitTranslate()
{
    const float shaftLength = m_worldScale * AXIS_LENGTH * (1.0f - CONE_LENGTH);
    const float coneLength = m_worldScale * AXIS_LENGTH * CONE_LENGTH;
    const float coneRadius = m_worldScale * CONE_RADIUS;

    for (const AxisHandle &axis : m_axes) {
        const bool hovered = m_hoveredAxis == axis.axis;
        const bool active = m_activeAxis == axis.axis;
        const glm::vec4 color = s_axisColor(axis.axis, hovered, active);

        const glm::vec3 shaftEnd = m_center + axis.direction * shaftLength;
        m_handleBatch.line(m_center, shaftEnd, color, LINE_THICKNESS);
        m_handleBatch.cone(shaftEnd, shaftEnd + axis.direction * coneLength, coneRadius, color, CONE_SEGMENTS);
    }

    submitPlanes();
}

void TransformGizmo::submitPlanes()
{
    for (const PlaneHandle &plane : m_planes) {
        const bool hovered = m_hoveredAxis == plane.axis;
        const bool active = m_activeAxis == plane.axis;
        const glm::vec4 fill = s_planeFill(plane.axis, hovered, active);
        const glm::vec4 outline = glm::vec4(glm::vec3(fill), 1.0f);

        m_handleBatch.quadFilled(plane.corners[0], plane.corners[1], plane.corners[2], plane.corners[3], fill);
        m_handleBatch.quad(plane.corners[0], plane.corners[1], plane.corners[2], plane.corners[3], outline,
                           PLANE_OUTLINE_THICKNESS);
    }
}

void TransformGizmo::submitRotate()
{
    const bool dragging = m_activeAxis != AXIS_NONE;

    for (const RingHandle &ring : m_rings) {
        // A turn in progress keeps only the ring being turned, and keeps all of it, so the angle
        // covered stays readable past the halfway point
        if (dragging && m_activeAxis != ring.axis) {
            continue;
        }

        // A ring keeps its axis colour while it is turned, since the axis is the thing being read
        const bool lit = m_hoveredAxis == ring.axis || m_activeAxis == ring.axis;
        const glm::vec4 color = s_axisColor(ring.axis, lit, false);
        const size_t count = ring.points.size();

        if (dragging) {
            m_handleBatch.polyline(ring.points, color, LINE_THICKNESS, true);
            continue;
        }

        for (size_t point = 0; point < count; ++point) {
            const size_t next = (point + 1) % count;
            if (ring.pointsFacing[point] == 0u || ring.pointsFacing[next] == 0u) {
                continue;
            }

            m_handleBatch.line(ring.points[point], ring.points[next], color, LINE_THICKNESS);
        }
    }
}

void TransformGizmo::submitScale()
{
    const float handleSize = m_worldScale * SCALE_HANDLE_SIZE;

    for (const AxisHandle &axis : m_axes) {
        const bool hovered = m_hoveredAxis == axis.axis;
        const bool active = m_activeAxis == axis.axis;
        const glm::vec4 color = s_axisColor(axis.axis, hovered, active);

        // Oriented by the gizmo's own basis, so a knob sits square to its axis in local space too
        const glm::mat4 knob(glm::vec4(m_basis[0], 0.0f), glm::vec4(m_basis[1], 0.0f), glm::vec4(m_basis[2], 0.0f),
                             glm::vec4(axis.tip, 1.0f));

        m_handleBatch.line(m_center, axis.tip, color, LINE_THICKNESS);
        m_handleBatch.boxFilled(knob, glm::vec3(-handleSize), glm::vec3(handleSize), color);
    }

    submitPlanes();
}

void TransformGizmo::submitShapes(const Params &params, Rapture::GizmoDrawList &drawList)
{
    m_handleBatch.reset();

    switch (params.operation) {
    case OPERATION_TRANSLATE:
        submitTranslate();
        break;
    case OPERATION_ROTATE:
        submitRotate();
        break;
    case OPERATION_SCALE:
        submitScale();
        break;
    case OPERATION_COUNT:
        break;
    }

    drawList.submit(m_handleBatch);
}

TransformGizmo::Result TransformGizmo::update(const Params &params, Rapture::GizmoDrawList &drawList)
{
    Result result;
    result.operation = params.operation;

    if (params.viewportSize.x < 1.0f || params.viewportSize.y < 1.0f) {
        return result;
    }

    m_viewportSize = params.viewportSize;
    m_viewProj = params.projection * params.view;
    m_invViewProj = glm::inverse(m_viewProj);

    buildHandles(params);

    if (m_activeAxis == AXIS_NONE) {
        m_hoveredAxis = hitTest(params);

        if (params.pressed && m_hoveredAxis != AXIS_NONE) {
            beginDrag(params);
        }
    }

    if (m_activeAxis != AXIS_NONE) {
        switch (m_activeOperation) {
        case OPERATION_TRANSLATE: {
            const glm::vec3 total = solveTranslate(params);
            result.deltaPosition = total - m_appliedTranslate;
            m_appliedTranslate = total;
            break;
        }
        case OPERATION_ROTATE: {
            const float total = solveRotate(params);
            result.deltaRotation = m_dragBasis[m_activeAxis - AXIS_X] * (total - m_appliedRotation);
            m_appliedRotation = total;
            break;
        }
        case OPERATION_SCALE: {
            const glm::vec3 total = solveScale(params);
            result.deltaScale = total / m_appliedScale;
            m_appliedScale = total;
            break;
        }
        case OPERATION_COUNT:
            break;
        }

        result.active = true;
        result.axis = m_activeAxis;
        result.operation = m_activeOperation;
        result.pivot = m_dragCenter;
        result.basis = glm::mat3(m_dragBasis[0], m_dragBasis[1], m_dragBasis[2]);

        if (params.released) {
            m_activeAxis = AXIS_NONE;
        }
    }

    result.hovered = m_hoveredAxis != AXIS_NONE || result.active;

    updateValueLabel(params);
    submitShapes(params, drawList);

    return result;
}

} // namespace gizmo
