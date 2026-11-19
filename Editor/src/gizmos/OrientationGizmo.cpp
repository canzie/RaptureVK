#include "OrientationGizmo.h"

#include <components/input_events.h>
#include <modules/color.h>
#include <parsers/freetype/font_registry.h>

#include <algorithm>
#include <string>

#define COL_AXIS_X Amethyst::Color4::fromHex(0xC25B5B)
#define COL_AXIS_Y Amethyst::Color4::fromHex(0x6FA35B)
#define COL_AXIS_Z Amethyst::Color4::fromHex(0x4F7FC9)
#define COL_AXIS_LABEL Amethyst::Color4::fromHex(0x141414)
#define COL_HOVER_RING Amethyst::Color4::fromHex(0xEEEEEE)

static constexpr float GIZMO_SIZE = 120.0f;
static constexpr float AXIS_LENGTH = 40.0f;
static constexpr float AXIS_LINE_WIDTH = 2.0f;
static constexpr float POSITIVE_END_RADIUS = 9.0f;
static constexpr float NEGATIVE_END_RADIUS = 7.0f;
static constexpr float NEGATIVE_END_RING_WIDTH = 2.0f;
static constexpr float NEGATIVE_END_FILL_OPACITY = 0.2f;
static constexpr float HOVER_RING_WIDTH = 1.5f;
static constexpr float LABEL_FONT_SIZE = 11.0f;
static constexpr std::string_view LABEL_FONT = "Roboto Bold";

static constexpr std::array<std::string_view, 3> AXIS_LABELS = {"X", "Y", "Z"};

static Amethyst::Color4 s_colorForAxis(uint8_t axis)
{
    switch (axis) {
    case 0:
        return COL_AXIS_X;
    case 1:
        return COL_AXIS_Y;
    default:
        return COL_AXIS_Z;
    }
}

static float s_radiusForAxisEnd(bool positive)
{
    return positive ? POSITIVE_END_RADIUS : NEGATIVE_END_RADIUS;
}

OrientationGizmo::OrientationGizmo()
{
    addClass("orientation-gizmo");
    setBaseProperties({.interactable = true, .size = Amethyst::UDim2::fromOffset(GIZMO_SIZE, GIZMO_SIZE)});
    redraw();
}

void OrientationGizmo::setViewRotation(const glm::quat &viewRotation)
{
    if (viewRotation == m_viewRotation) {
        return;
    }
    m_viewRotation = viewRotation;
    redraw();
}

void OrientationGizmo::redraw()
{
    const glm::quat worldToView = glm::inverse(m_viewRotation);
    const Amethyst::vec2 gizmoCenter(GIZMO_SIZE * 0.5f, GIZMO_SIZE * 0.5f);

    for (uint8_t axis = 0; axis < 3; axis++) {
        for (uint8_t sign = 0; sign < 2; sign++) {
            const bool positive = sign == 0;
            glm::vec3 direction(0.0f);
            direction[axis] = positive ? 1.0f : -1.0f;

            // the camera looks down its -Z, so a larger view z is nearer the viewer; screen y grows downwards
            const glm::vec3 inView = worldToView * direction;
            m_axisEnds[axis * 2 + sign] = {
                .direction = direction,
                .center = gizmoCenter + Amethyst::vec2(inView.x, -inView.y) * AXIS_LENGTH,
                .depth = inView.z,
                .axis = axis,
                .positive = positive,
            };
        }
    }
    std::ranges::sort(m_axisEnds, [](const AxisEnd &a, const AxisEnd &b) { return a.depth < b.depth; });

    const Amethyst::FontId labelFont = Amethyst::FontRegistry::instance().findFont(LABEL_FONT);

    clear();
    for (size_t i = 0; i < m_axisEnds.size(); i++) {
        const AxisEnd &end = m_axisEnds[i];
        const Amethyst::Color4 color = s_colorForAxis(end.axis);
        const float radius = s_radiusForAxisEnd(end.positive);

        if (end.positive) {
            drawLine(gizmoCenter, end.center, color, AXIS_LINE_WIDTH);
            drawCircleFilled(end.center, radius, color);
            const Amethyst::vec2 labelBox(radius * 2.0f, radius * 2.0f);
            drawText(std::string(AXIS_LABELS[end.axis]), end.center - labelBox * 0.5f, labelBox, labelFont, LABEL_FONT_SIZE,
                     COL_AXIS_LABEL, Amethyst::TextXAlignment::CENTER, Amethyst::TextYAlignment::CENTER, 1);
        } else {
            drawCircleFilled(end.center, radius, Amethyst::Color4(color.r, color.g, color.b, NEGATIVE_END_FILL_OPACITY));
            drawCircleStroke(end.center, radius, color, NEGATIVE_END_RING_WIDTH);
        }

        if (static_cast<int32_t>(i) == m_hoveredAxisEnd) {
            drawCircleStroke(end.center, radius + HOVER_RING_WIDTH, COL_HOVER_RING, HOVER_RING_WIDTH);
        }
    }
}

int32_t OrientationGizmo::findAxisEndAt(Amethyst::vec2 localPosition) const
{
    // the ends are drawn back to front, so the nearest one under the cursor is the last that contains it
    for (int32_t i = static_cast<int32_t>(m_axisEnds.size()) - 1; i >= 0; i--) {
        const AxisEnd &end = m_axisEnds[static_cast<size_t>(i)];
        const Amethyst::vec2 offset = localPosition - end.center;
        const float radius = s_radiusForAxisEnd(end.positive);
        if (offset.x * offset.x + offset.y * offset.y <= radius * radius) {
            return i;
        }
    }
    return NO_AXIS_END;
}

void OrientationGizmo::setHoveredAxisEnd(int32_t axisEnd)
{
    if (axisEnd == m_hoveredAxisEnd) {
        return;
    }
    m_hoveredAxisEnd = axisEnd;
    redraw();
}

Amethyst::EventResult OrientationGizmo::onInputBegan(const Amethyst::InputObject &input)
{
    if (input.type != Amethyst::InputType::MOUSE_BUTTON_1) {
        return Canvas::onInputBegan(input);
    }

    const int32_t axisEnd = findAxisEndAt(Amethyst::vec2(input.position.x, input.position.y) - absolutePosition);
    if (axisEnd == NO_AXIS_END) {
        return Canvas::onInputBegan(input);
    }

    // clicking an end shows the view from that side, looking back along the axis
    const glm::vec3 lookDirection = -m_axisEnds[static_cast<size_t>(axisEnd)].direction;
    if (std::function<void(const glm::vec3 &)> clicked = onAxisEndClicked; clicked != nullptr) {
        clicked(lookDirection);
    }
    return Amethyst::EventResult::CONSUMED;
}

Amethyst::EventResult OrientationGizmo::onMouseMoved(int32_t x, int32_t y)
{
    setHoveredAxisEnd(findAxisEndAt(Amethyst::vec2(static_cast<float>(x), static_cast<float>(y)) - absolutePosition));
    return Canvas::onMouseMoved(x, y);
}

Amethyst::EventResult OrientationGizmo::onMouseLeave()
{
    setHoveredAxisEnd(NO_AXIS_END);
    return Canvas::onMouseLeave();
}
