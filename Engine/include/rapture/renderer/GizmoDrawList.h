#ifndef RAPTURE__GIZMO_DRAW_LIST_H
#define RAPTURE__GIZMO_DRAW_LIST_H

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Rapture {

/**
 * @brief How a gizmo resolves against the scene depth
 */
enum DepthMode {
    DEPTH_MODE_TESTED,          ///< occluded by scene geometry
    DEPTH_MODE_ALWAYS_IN_FRONT, ///< drawn over scene geometry
    DEPTH_MODE_COUNT
};

/**
 * @brief How a filled gizmo is shaded, which lines take no part in
 */
enum GizmoShadingMode {
    GIZMO_SHADING_MODE_SOLID,     ///< flat, in the colour it was submitted under
    GIZMO_SHADING_MODE_WIREFRAME, ///< the edges of its triangles
    GIZMO_SHADING_MODE_SHADED,    ///< lit, so its form reads
    GIZMO_SHADING_MODE_COUNT
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
 * @brief One corner of a filled gizmo
 */
struct GizmoVertex {
    alignas(16) glm::vec3 position;
    alignas(4) uint32_t color;
};

/**
 * @brief A run of filled triangles in a draw list that a query tests, and what it answers with
 */
struct PickCandidate {
    uint64_t userData = 0;
    DepthMode depthMode = DEPTH_MODE_TESTED;
    GizmoShadingMode shadingMode = GIZMO_SHADING_MODE_SOLID;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    glm::vec3 worldMin{0.0f};
    glm::vec3 worldMax{0.0f};
};

/**
 * @brief Gizmos built together and submitted to a draw list as one
 *
 * Owns what it is built from, so it can be kept, submitted to more than one draw list, and built
 * without a list to build into.
 */
class GizmoBatch {
  public:
    GizmoBatch(DepthMode depthMode, GizmoShadingMode shadingMode);

    /**
     * @param depthMode How these gizmos resolve against scene geometry
     * @param shadingMode How these gizmos are shaded
     * @param userData What a query over these gizmos reports
     */
    GizmoBatch(DepthMode depthMode, GizmoShadingMode shadingMode, uint64_t userData);

    void setDepthMode(DepthMode depthMode) { m_depthMode = depthMode; }
    void setShadingMode(GizmoShadingMode shadingMode) { m_shadingMode = shadingMode; }
    void setUserData(uint64_t userData) { m_userData = userData; }

    DepthMode getDepthMode() const { return m_depthMode; }
    GizmoShadingMode getShadingMode() const { return m_shadingMode; }
    const std::optional<uint64_t> &getUserData() const { return m_userData; }
    const std::vector<LineSegment> &getSegments() const { return m_segments; }
    const std::vector<GizmoVertex> &getTriangleVertices() const { return m_triangleVertices; }

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
    void polyline(std::span<const glm::vec3> points, const glm::vec4 &color, float thickness = 1.0f, bool closed = false);

    /**
     * @brief The outline of a triangle
     * @param a First corner
     * @param b Second corner
     * @param c Third corner
     * @param color Line colour
     * @param thickness Line width in pixels
     */
    void triangle(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec4 &color, float thickness = 1.0f);

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
    void quadFilled(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d, const glm::vec4 &color);

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
     * @brief The six solid faces of a box under a transform
     * @param transform Places the box, which spans min to max before it is applied
     * @param min Corner the box runs from
     * @param max Corner the box runs to
     * @param color Fill colour
     */
    void boxFilled(const glm::mat4 &transform, const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color);

    /**
     * @brief A solid cone, capped at its base
     * @param base World position the base is centred on
     * @param tip World position the cone narrows to
     * @param radius Base radius in world units
     * @param color Fill colour
     * @param segments Triangles the base is approximated by
     */
    void cone(const glm::vec3 &base, const glm::vec3 &tip, float radius, const glm::vec4 &color, uint32_t segments = 16);

    /**
     * @brief The outline of a circle
     * @param center World position the circle is centred on
     * @param normal Axis the circle faces along, which need not be normalized
     * @param radius Circle radius in world units
     * @param color Line colour
     * @param thickness Line width in pixels
     * @param segments Lines the circle is approximated by
     */
    void circle(const glm::vec3 &center, const glm::vec3 &normal, float radius, const glm::vec4 &color, float thickness = 1.0f,
                uint32_t segments = 32);

    /**
     * @brief Three circles on the cardinal planes
     * @param center World position the sphere is centred on
     * @param radius Sphere radius in world units
     * @param color Line colour
     * @param thickness Line width in pixels
     * @param segments Lines each circle is approximated by
     */
    void sphere(const glm::vec3 &center, float radius, const glm::vec4 &color, float thickness = 1.0f, uint32_t segments = 32);

    /**
     * @brief A solid sphere
     * @param center World position the sphere is centred on
     * @param radius Sphere radius in world units
     * @param color Fill colour
     * @param segments Divisions around the sphere's axis
     * @param rings Divisions from pole to pole
     */
    void sphereFilled(const glm::vec3 &center, float radius, const glm::vec4 &color, uint32_t segments = 12, uint32_t rings = 8);

    void reset();

  private:
    // TODO: a batch allocates for its own geometry and submit copies it into the list again, so
    // investigate an arena both can draw from
    std::vector<LineSegment> m_segments;
    std::vector<GizmoVertex> m_triangleVertices;
    DepthMode m_depthMode;
    GizmoShadingMode m_shadingMode;
    std::optional<uint64_t> m_userData;
};

/**
 * @brief The gizmos drawn over one view for a frame
 *
 * Immediate: whatever is submitted over a frame is drawn once and dropped, so a producer that wants
 * to stay visible submits again next frame and one that is dismissed simply stops. What was drawn
 * stays readable until submission begins again, so a query between frames answers against the
 * gizmos the frame was drawn with.
 */
class GizmoDrawList {
  public:
    /**
     * @brief Adds a batch's gizmos to this list
     * @param batch The batch to add, which is left as it was
     */
    void submit(const GizmoBatch &batch);

    /**
     * @brief Adds several batches' gizmos to this list
     * @param batches The batches to add, which are left as they were
     */
    void submit(std::span<const GizmoBatch> batches);

    /**
     * @brief Whether anything was submitted this frame
     * @return True where no depth mode holds a gizmo
     */
    bool empty() const;

    /**
     * @brief Whether what the list holds has already been drawn
     */
    bool isDrawn() const { return m_drawn; }

    /**
     * @brief Marks everything submitted as drawn
     */
    void markDrawn() { m_drawn = true; }

    /**
     * @brief Drops everything submitted, drawn or not
     */
    void reset();

    const std::vector<LineSegment> &getSegments(DepthMode depthMode) const { return m_segments[depthMode]; }
    const std::vector<GizmoVertex> &getTriangleVertices(DepthMode depthMode, GizmoShadingMode shadingMode) const
    {
        return m_triangleVertices[depthMode][shadingMode];
    }
    const std::vector<PickCandidate> &getPickCandidates() const { return m_pickCandidates; }

  private:
    /**
     * @brief Drops what has been drawn, so the gizmos submitted after it stand alone
     */
    void clearIfDrawn();

  private:
    std::array<std::vector<LineSegment>, DEPTH_MODE_COUNT> m_segments;
    std::array<std::array<std::vector<GizmoVertex>, GIZMO_SHADING_MODE_COUNT>, DEPTH_MODE_COUNT> m_triangleVertices;
    std::vector<PickCandidate> m_pickCandidates;
    bool m_drawn = false;
};

} // namespace Rapture

#endif // RAPTURE__GIZMO_DRAW_LIST_H
