#include "renderer/ImmediateDrawList.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Rapture {

static uint32_t s_packColor(const glm::vec4 &color)
{
    const glm::vec4 clamped = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
    const uint32_t r = static_cast<uint32_t>(clamped.r * 255.0f + 0.5f);
    const uint32_t g = static_cast<uint32_t>(clamped.g * 255.0f + 0.5f);
    const uint32_t b = static_cast<uint32_t>(clamped.b * 255.0f + 0.5f);
    const uint32_t a = static_cast<uint32_t>(clamped.a * 255.0f + 0.5f);
    return r | (g << 8) | (b << 16) | (a << 24);
}

/**
 * @brief The eight corners of a box, indexed by one bit per axis
 * @param min Corner the box runs from
 * @param max Corner the box runs to
 * @param corners Receives the corners, bit 0 selecting max.x, bit 1 max.y and bit 2 max.z
 */
static void s_buildBoxCorners(const glm::vec3 &min, const glm::vec3 &max, glm::vec3 (&corners)[8])
{
    for (uint32_t i = 0; i < 8; ++i) {
        corners[i] = glm::vec3((i & 1u) != 0u ? max.x : min.x, (i & 2u) != 0u ? max.y : min.y,
                               (i & 4u) != 0u ? max.z : min.z);
    }
}

/**
 * @brief An orthonormal pair spanning the plane a normal faces along
 * @param normal Axis the plane faces along, which need not be normalized
 * @param tangent Receives the first axis of the plane
 * @param bitangent Receives the second axis of the plane
 */
static void s_buildPlaneBasis(const glm::vec3 &normal, glm::vec3 &tangent, glm::vec3 &bitangent)
{
    const glm::vec3 axis = glm::normalize(normal);
    const glm::vec3 reference = std::abs(axis.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    tangent = glm::normalize(glm::cross(reference, axis));
    bitangent = glm::cross(axis, tangent);
}

void ShapeSubmission::line(const glm::vec3 &start, const glm::vec3 &end, const glm::vec4 &color, float thickness)
{
    m_segments.push_back(LineSegment{start, thickness, end, s_packColor(color)});
}

void ShapeSubmission::polyline(std::span<const glm::vec3> points, const glm::vec4 &color, float thickness, bool closed)
{
    if (points.size() < 2) {
        return;
    }

    const uint32_t packed = s_packColor(color);
    for (size_t i = 1; i < points.size(); ++i) {
        m_segments.push_back(LineSegment{points[i - 1], thickness, points[i], packed});
    }

    if (closed) {
        m_segments.push_back(LineSegment{points.back(), thickness, points.front(), packed});
    }
}

void ShapeSubmission::triangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec4 &color,
                               float thickness)
{
    const uint32_t packed = s_packColor(color);
    m_segments.push_back(LineSegment{a, thickness, b, packed});
    m_segments.push_back(LineSegment{b, thickness, c, packed});
    m_segments.push_back(LineSegment{c, thickness, a, packed});
}

void ShapeSubmission::triangleFilled(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec4 &color)
{
    const uint32_t packed = s_packColor(color);
    m_triangleVertices.push_back(ShapeVertex{a, packed});
    m_triangleVertices.push_back(ShapeVertex{b, packed});
    m_triangleVertices.push_back(ShapeVertex{c, packed});
}

void ShapeSubmission::quad(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
                           const glm::vec4 &color, float thickness)
{
    const uint32_t packed = s_packColor(color);
    m_segments.push_back(LineSegment{a, thickness, b, packed});
    m_segments.push_back(LineSegment{b, thickness, c, packed});
    m_segments.push_back(LineSegment{c, thickness, d, packed});
    m_segments.push_back(LineSegment{d, thickness, a, packed});
}

void ShapeSubmission::quadFilled(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
                                 const glm::vec4 &color)
{
    triangleFilled(a, b, c, color);
    triangleFilled(a, c, d, color);
}

void ShapeSubmission::box(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color, float thickness)
{
    glm::vec3 corners[8];
    s_buildBoxCorners(min, max, corners);

    const uint32_t packed = s_packColor(color);
    for (uint32_t i = 0; i < 8; ++i) {
        for (uint32_t bit = 1; bit <= 4; bit <<= 1) {
            if ((i & bit) != 0u) {
                continue;
            }
            m_segments.push_back(LineSegment{corners[i], thickness, corners[i | bit], packed});
        }
    }
}

void ShapeSubmission::box(const glm::mat4 &transform, const glm::vec3 &min, const glm::vec3 &max,
                          const glm::vec4 &color, float thickness)
{
    glm::vec3 corners[8];
    s_buildBoxCorners(min, max, corners);
    for (glm::vec3 &corner : corners) {
        corner = glm::vec3(transform * glm::vec4(corner, 1.0f));
    }

    const uint32_t packed = s_packColor(color);
    for (uint32_t i = 0; i < 8; ++i) {
        for (uint32_t bit = 1; bit <= 4; bit <<= 1) {
            if ((i & bit) != 0u) {
                continue;
            }
            m_segments.push_back(LineSegment{corners[i], thickness, corners[i | bit], packed});
        }
    }
}

void ShapeSubmission::boxFilled(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color)
{
    glm::vec3 corners[8];
    s_buildBoxCorners(min, max, corners);

    static constexpr uint32_t FACES[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
                                             {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};

    for (const uint32_t (&face)[4] : FACES) {
        quadFilled(corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]], color);
    }
}

void ShapeSubmission::cone(const glm::vec3 &base, const glm::vec3 &tip, float radius, const glm::vec4 &color,
                           uint32_t segments)
{
    const glm::vec3 axis = tip - base;
    if (segments < 3 || glm::dot(axis, axis) <= 0.0f) {
        return;
    }

    glm::vec3 tangent;
    glm::vec3 bitangent;
    s_buildPlaneBasis(axis, tangent, bitangent);

    const float step = glm::two_pi<float>() / static_cast<float>(segments);

    glm::vec3 previous = base + tangent * radius;
    for (uint32_t i = 1; i <= segments; ++i) {
        const float angle = step * static_cast<float>(i);
        const glm::vec3 current = base + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        triangleFilled(previous, current, tip, color);
        triangleFilled(current, previous, base, color);
        previous = current;
    }
}

void ShapeSubmission::circle(const glm::vec3 &center, const glm::vec3 &normal, float radius, const glm::vec4 &color,
                             float thickness, uint32_t segments)
{
    if (segments < 3) {
        return;
    }

    glm::vec3 tangent;
    glm::vec3 bitangent;
    s_buildPlaneBasis(normal, tangent, bitangent);

    const uint32_t packed = s_packColor(color);
    const float step = glm::two_pi<float>() / static_cast<float>(segments);

    glm::vec3 previous = center + tangent * radius;
    for (uint32_t i = 1; i <= segments; ++i) {
        const float angle = step * static_cast<float>(i);
        const glm::vec3 current = center + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        m_segments.push_back(LineSegment{previous, thickness, current, packed});
        previous = current;
    }
}

void ShapeSubmission::sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, float thickness,
                             uint32_t segments)
{
    circle(center, glm::vec3(1.0f, 0.0f, 0.0f), radius, color, thickness, segments);
    circle(center, glm::vec3(0.0f, 1.0f, 0.0f), radius, color, thickness, segments);
    circle(center, glm::vec3(0.0f, 0.0f, 1.0f), radius, color, thickness, segments);
}

bool ImmediateDrawList::empty() const
{
    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        if (!m_segments[mode].empty() || !m_triangleVertices[mode].empty()) {
            return false;
        }
    }

    return true;
}

void ImmediateDrawList::clear()
{
    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        m_segments[mode].clear();
        m_triangleVertices[mode].clear();
    }
}

} // namespace Rapture
