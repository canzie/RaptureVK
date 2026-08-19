#ifndef RAPTURE__IMMEDIATE_DRAW_LIST_H
#define RAPTURE__IMMEDIATE_DRAW_LIST_H

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief How a shape resolves against the scene depth
 */
enum DepthMode {
    DEPTH_MODE_TESTED,          ///< occluded by scene geometry
    DEPTH_MODE_ALWAYS_IN_FRONT, ///< drawn over scene geometry
    DEPTH_MODE_COUNT
};

/**
 * @brief A line between two points, expanded to its pixel thickness while drawing
 */
struct LineSegment {
    alignas(16) glm::vec3 start;
    alignas(4) float thickness;
    alignas(16) glm::vec3 end;
    alignas(4) uint32_t color;
};

/**
 * @brief One corner of a filled shape
 */
struct ShapeVertex {
    alignas(16) glm::vec3 position;
    alignas(4) uint32_t color;
};

/**
 * @brief Adds shapes to one depth mode of a draw list
 *
 * Held only for as long as it takes to emit, and any number may be open at once, so several
 * producers can add to a list over a frame without agreeing on an order.
 */
class ShapeSubmission {
  public:
    ShapeSubmission(std::vector<LineSegment> &segments, std::vector<ShapeVertex> &triangleVertices)
        : m_segments(segments), m_triangleVertices(triangleVertices)
    {
    }

    /**
     * @brief A line between two points
     * @param start World position the line runs from
     * @param end World position the line runs to
     * @param color Line colour
     * @param thickness Line width in pixels
     */
    void line(const glm::vec3 &start, const glm::vec3 &end, const glm::vec4 &color, float thickness = 1.0f);

    /**
     * @brief A run of connected lines
     * @param points World positions the run passes through
     * @param color Line colour
     * @param thickness Line width in pixels
     * @param closed Whether a line joins the last point back to the first
     */
    void polyline(std::span<const glm::vec3> points, const glm::vec4 &color, float thickness = 1.0f,
                  bool closed = false);

    /**
     * @brief The outline of a triangle
     * @param a First corner
     * @param b Second corner
     * @param c Third corner
     * @param color Line colour
     * @param thickness Line width in pixels
     */
    void triangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec4 &color,
                  float thickness = 1.0f);

    /**
     * @brief A solid triangle
     * @param a First corner
     * @param b Second corner
     * @param c Third corner
     * @param color Fill colour
     */
    void triangleFilled(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec4 &color);

    /**
     * @brief The outline of a quad
     * @param a First corner, wound through to d
     * @param b Second corner
     * @param c Third corner
     * @param d Fourth corner
     * @param color Line colour
     * @param thickness Line width in pixels
     */
    void quad(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d, const glm::vec4 &color,
              float thickness = 1.0f);

    /**
     * @brief A solid quad
     * @param a First corner, wound through to d
     * @param b Second corner
     * @param c Third corner
     * @param d Fourth corner
     * @param color Fill colour
     */
    void quadFilled(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
                    const glm::vec4 &color);

    /**
     * @brief The twelve edges of an axis aligned box
     * @param min Corner the box runs from
     * @param max Corner the box runs to
     * @param color Line colour
     * @param thickness Line width in pixels
     */
    void box(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color, float thickness = 1.0f);

    /**
     * @brief The twelve edges of a box under a transform
     * @param transform Places the box, which spans min to max before it is applied
     * @param min Corner the box runs from
     * @param max Corner the box runs to
     * @param color Line colour
     * @param thickness Line width in pixels
     */
    void box(const glm::mat4 &transform, const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color,
             float thickness = 1.0f);

    /**
     * @brief The six solid faces of an axis aligned box
     * @param min Corner the box runs from
     * @param max Corner the box runs to
     * @param color Fill colour
     */
    void boxFilled(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color);

    /**
     * @brief A solid cone, capped at its base
     * @param base World position the base is centred on
     * @param tip World position the cone narrows to
     * @param radius Base radius in world units
     * @param color Fill colour
     * @param segments Triangles the base is approximated by
     */
    void cone(const glm::vec3 &base, const glm::vec3 &tip, float radius, const glm::vec4 &color,
              uint32_t segments = 16);

    /**
     * @brief The outline of a circle
     * @param center World position the circle is centred on
     * @param normal Axis the circle faces along, which need not be normalized
     * @param radius Circle radius in world units
     * @param color Line colour
     * @param thickness Line width in pixels
     * @param segments Lines the circle is approximated by
     */
    void circle(const glm::vec3 &center, const glm::vec3 &normal, float radius, const glm::vec4 &color,
                float thickness = 1.0f, uint32_t segments = 32);

    /**
     * @brief Three circles on the cardinal planes
     * @param center World position the sphere is centred on
     * @param radius Sphere radius in world units
     * @param color Line colour
     * @param thickness Line width in pixels
     * @param segments Lines each circle is approximated by
     */
    void sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, float thickness = 1.0f,
                uint32_t segments = 32);

  private:
    std::vector<LineSegment> &m_segments;
    std::vector<ShapeVertex> &m_triangleVertices;
};

/**
 * @brief The 3D shapes drawn over one view for a frame
 *
 * Immediate: whatever is submitted over a frame is drawn once and dropped, so a producer that wants
 * to stay visible submits again next frame and one that is dismissed simply stops.
 */
class ImmediateDrawList {
  public:
    /**
     * @brief Open a submission for shapes sharing a depth mode
     * @param depthMode How the submission's shapes resolve against scene geometry
     * @return The submission to emit into
     */
    ShapeSubmission getSubmission(DepthMode depthMode)
    {
        return ShapeSubmission(m_segments[depthMode], m_triangleVertices[depthMode]);
    }

    /**
     * @brief Whether anything was submitted this frame
     * @return True where no depth mode holds a shape
     */
    bool empty() const;

    /**
     * @brief Drop everything submitted, keeping the storage for the next frame
     */
    void clear();

    const std::vector<LineSegment> &getSegments(DepthMode depthMode) const { return m_segments[depthMode]; }
    const std::vector<ShapeVertex> &getTriangleVertices(DepthMode depthMode) const
    {
        return m_triangleVertices[depthMode];
    }

  private:
    std::array<std::vector<LineSegment>, DEPTH_MODE_COUNT> m_segments;
    std::array<std::vector<ShapeVertex>, DEPTH_MODE_COUNT> m_triangleVertices;
};

} // namespace Rapture

#endif // RAPTURE__IMMEDIATE_DRAW_LIST_H
