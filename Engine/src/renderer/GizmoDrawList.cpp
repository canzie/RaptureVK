#include "renderer/GizmoDrawList.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <limits>

namespace Rapture {

// The four corners of each box face, wound consistently, indexed as s_buildBoxCorners numbers them
static constexpr uint32_t BOX_FACES[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};

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
        corners[i] = glm::vec3((i & 1u) != 0u ? max.x : min.x, (i & 2u) != 0u ? max.y : min.y, (i & 4u) != 0u ? max.z : min.z);
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

GizmoBatch::GizmoBatch(DepthMode depthMode, GizmoShadingMode shadingMode) : m_depthMode(depthMode), m_shadingMode(shadingMode) {}

GizmoBatch::GizmoBatch(DepthMode depthMode, GizmoShadingMode shadingMode, uint64_t userData)
    : m_depthMode(depthMode), m_shadingMode(shadingMode), m_userData(userData)
{
}

void GizmoBatch::line(const glm::vec3 &start, const glm::vec3 &end, const glm::vec4 &color, float thickness)
{
    m_segments.push_back(LineSegment{start, thickness, end, s_packColor(color)});
}

void GizmoBatch::polyline(std::span<const glm::vec3> points, const glm::vec4 &color, float thickness, bool closed)
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

void GizmoBatch::triangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec4 &color, float thickness)
{
    const uint32_t packed = s_packColor(color);
    m_segments.push_back(LineSegment{a, thickness, b, packed});
    m_segments.push_back(LineSegment{b, thickness, c, packed});
    m_segments.push_back(LineSegment{c, thickness, a, packed});
}

void GizmoBatch::triangleFilled(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec4 &color)
{
    const uint32_t packed = s_packColor(color);
    m_triangleVertices.push_back(GizmoVertex{a, packed});
    m_triangleVertices.push_back(GizmoVertex{b, packed});
    m_triangleVertices.push_back(GizmoVertex{c, packed});
}

void GizmoBatch::quad(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d, const glm::vec4 &color,
                      float thickness)
{
    const uint32_t packed = s_packColor(color);
    m_segments.push_back(LineSegment{a, thickness, b, packed});
    m_segments.push_back(LineSegment{b, thickness, c, packed});
    m_segments.push_back(LineSegment{c, thickness, d, packed});
    m_segments.push_back(LineSegment{d, thickness, a, packed});
}

void GizmoBatch::quadFilled(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d, const glm::vec4 &color)
{
    triangleFilled(a, b, c, color);
    triangleFilled(a, c, d, color);
}

void GizmoBatch::box(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color, float thickness)
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

void GizmoBatch::box(const glm::mat4 &transform, const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color,
                     float thickness)
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

void GizmoBatch::boxFilled(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color)
{
    glm::vec3 corners[8];
    s_buildBoxCorners(min, max, corners);

    for (const uint32_t (&face)[4] : BOX_FACES) {
        quadFilled(corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]], color);
    }
}

void GizmoBatch::boxFilled(const glm::mat4 &transform, const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color)
{
    glm::vec3 corners[8];
    s_buildBoxCorners(min, max, corners);
    for (glm::vec3 &corner : corners) {
        corner = glm::vec3(transform * glm::vec4(corner, 1.0f));
    }

    for (const uint32_t (&face)[4] : BOX_FACES) {
        quadFilled(corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]], color);
    }
}

void GizmoBatch::cone(const glm::vec3 &base, const glm::vec3 &tip, float radius, const glm::vec4 &color, uint32_t segments)
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

void GizmoBatch::circle(const glm::vec3 &center, const glm::vec3 &normal, float radius, const glm::vec4 &color, float thickness,
                        uint32_t segments)
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

void GizmoBatch::sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, float thickness, uint32_t segments)
{
    circle(center, glm::vec3(1.0f, 0.0f, 0.0f), radius, color, thickness, segments);
    circle(center, glm::vec3(0.0f, 1.0f, 0.0f), radius, color, thickness, segments);
    circle(center, glm::vec3(0.0f, 0.0f, 1.0f), radius, color, thickness, segments);
}

void GizmoDrawList::submit(const GizmoBatch &batch)
{
    clearIfDrawn();

    const std::vector<LineSegment> &batchSegments = batch.getSegments();
    std::vector<LineSegment> &segments = m_segments[batch.getDepthMode()];
    segments.insert(segments.end(), batchSegments.begin(), batchSegments.end());

    const std::vector<GizmoVertex> &batchVertices = batch.getTriangleVertices();
    if (batchVertices.empty()) {
        return;
    }

    std::vector<GizmoVertex> &vertices = m_triangleVertices[batch.getDepthMode()][batch.getShadingMode()];
    const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), batchVertices.begin(), batchVertices.end());

    if (!batch.getUserData().has_value()) {
        return;
    }

    PickCandidate candidate;
    candidate.userData = *batch.getUserData();
    candidate.depthMode = batch.getDepthMode();
    candidate.shadingMode = batch.getShadingMode();
    candidate.firstVertex = firstVertex;
    candidate.vertexCount = static_cast<uint32_t>(batchVertices.size());
    candidate.worldMin = glm::vec3(std::numeric_limits<float>::max());
    candidate.worldMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (const GizmoVertex &vertex : batchVertices) {
        candidate.worldMin = glm::min(candidate.worldMin, vertex.position);
        candidate.worldMax = glm::max(candidate.worldMax, vertex.position);
    }

    m_pickCandidates.push_back(candidate);
}

void GizmoDrawList::submit(std::span<const GizmoBatch> batches)
{
    for (const GizmoBatch &batch : batches) {
        submit(batch);
    }
}

void GizmoBatch::sphereFilled(const glm::vec3 &center, float radius, const glm::vec4 &color, uint32_t segments, uint32_t rings)
{
    if (segments < 3 || rings < 2) {
        return;
    }

    const float ringStep = glm::pi<float>() / static_cast<float>(rings);
    const float segmentStep = glm::two_pi<float>() / static_cast<float>(segments);

    auto pointAt = [&](uint32_t ring, uint32_t segment) {
        const float phi = ringStep * static_cast<float>(ring);
        const float theta = segmentStep * static_cast<float>(segment);
        return center + radius * glm::vec3(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
    };

    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t segment = 0; segment < segments; ++segment) {
            const glm::vec3 topLeft = pointAt(ring, segment);
            const glm::vec3 topRight = pointAt(ring, segment + 1);
            const glm::vec3 bottomLeft = pointAt(ring + 1, segment);
            const glm::vec3 bottomRight = pointAt(ring + 1, segment + 1);

            // the poles collapse to a point, so their row is one triangle rather than two
            if (ring != 0) {
                triangleFilled(topLeft, bottomLeft, topRight, color);
            }
            if (ring + 1 != rings) {
                triangleFilled(topRight, bottomLeft, bottomRight, color);
            }
        }
    }
}

void GizmoBatch::reset()
{
    m_segments.clear();
    m_triangleVertices.clear();
}

bool GizmoDrawList::empty() const
{
    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        if (!m_segments[mode].empty()) {
            return false;
        }

        for (uint32_t shading = 0; shading < GIZMO_SHADING_MODE_COUNT; ++shading) {
            if (!m_triangleVertices[mode][shading].empty()) {
                return false;
            }
        }
    }

    return true;
}

void GizmoDrawList::clearIfDrawn()
{
    if (!m_drawn) {
        return;
    }

    reset();
}

void GizmoDrawList::reset()
{
    for (uint32_t mode = 0; mode < DEPTH_MODE_COUNT; ++mode) {
        m_segments[mode].clear();

        for (uint32_t shading = 0; shading < GIZMO_SHADING_MODE_COUNT; ++shading) {
            m_triangleVertices[mode][shading].clear();
        }
    }

    m_pickCandidates.clear();
    m_drawn = false;
}

} // namespace Rapture
