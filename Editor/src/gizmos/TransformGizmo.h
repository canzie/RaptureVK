#ifndef RAPTURE__TRANSFORM_GIZMO_H
#define RAPTURE__TRANSFORM_GIZMO_H

#include "renderer/ImmediateDrawList.h"

#include <amethyst/Amethyst.h>
#include <components/container.h>
#include <components/text_label.h>

#include <glm/glm.hpp>

#include <vector>

namespace gizmo {

/**
 * @brief The move, rotate and scale handles drawn over a selected object
 *
 * Answers what the cursor is over itself, since it placed the handles and so already knows their
 * screen-space form. Once a handle is grabbed the drag resolves against the constraint taken at
 * that moment and nothing is hit tested again until release.
 */
class TransformGizmo {
  public:
    enum Operation {
        OPERATION_TRANSLATE,
        OPERATION_ROTATE,
        OPERATION_SCALE,
        OPERATION_COUNT
    };

    enum Space {
        SPACE_LOCAL,
        SPACE_WORLD,
        SPACE_COUNT
    };

    /**
     * @brief Which handle of the gizmo something refers to
     */
    enum Axis {
        AXIS_NONE,
        AXIS_X,
        AXIS_Y,
        AXIS_Z,
        AXIS_XY,
        AXIS_XZ,
        AXIS_YZ,
        AXIS_XYZ,
        AXIS_COUNT
    };

    /**
     * @brief Increments a drag is quantised to while snapping is asked for
     */
    struct Snap {
        bool enabled = false;
        bool whileShiftHeld = true;
        float translate = 1.0f;
        float rotateDegrees = 15.0f;
        float scale = 0.1f;
    };

    /**
     * @brief What the gizmo is being shown for and what the cursor is doing this frame
     */
    struct Params {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::mat4 objectTransform{1.0f};
        glm::vec3 pivot{0.0f};

        glm::vec2 viewportSize{0.0f};
        glm::vec2 cursor{0.0f}; ///< cursor in viewport pixels, y down
        bool cursorInside = false;
        bool pressed = false;
        bool released = false;
        bool shiftHeld = false;

        Operation operation = OPERATION_TRANSLATE;
        Space space = SPACE_WORLD;
        Snap snap;
    };

    /**
     * @brief What the gizmo did this frame, in the object's parent space
     */
    struct Result {
        bool hovered = false;
        bool active = false;
        Axis axis = AXIS_NONE;
        Operation operation = OPERATION_TRANSLATE;
        glm::vec3 deltaPosition{0.0f};
        glm::vec3 deltaRotation{0.0f}; ///< world space axis scaled by the angle in radians
        glm::vec3 deltaScale{1.0f};    ///< per axis of basis
        glm::vec3 pivot{0.0f};         ///< world position a rotation or scale turns about
        glm::mat3 basis{1.0f};         ///< world space axes deltaScale is measured along
    };

  public:
    /**
     * @brief Gives the gizmo a place for 2D elements like text
     * @param container Parent for the gizmo's 2D elements, spanning the viewport so its offsets are viewport pixels
     */
    explicit TransformGizmo(Amethyst::Container *container);

    /**
     * @brief Hit test, advance any drag, and submit the handles to be drawn
     * @param params What to show and what the cursor is doing
     * @param drawList Draw list of the viewport the gizmo is shown in
     * @return What the gizmo did this frame
     */
    Result update(const Params &params, Rapture::ImmediateDrawList &drawList);

    /**
     * @brief Whether a handle is under the cursor or being dragged
     * @return True while the gizmo would act on a press
     */
    bool isHovered() const { return m_hoveredAxis != AXIS_NONE || m_activeAxis != AXIS_NONE; }

    /**
     * @brief Drop any drag in progress
     */
    void reset();

  private:
    /**
     * @brief One axis handle, an arrow for translate or a knob for scale
     */
    struct AxisHandle {
        Axis axis;
        glm::vec3 direction;
        glm::vec3 tip;
        glm::vec2 tipScreen;
    };

    /**
     * @brief One plane handle, a quad offset along the two axes it spans
     */
    struct PlaneHandle {
        Axis axis;
        glm::vec3 corners[4];
        glm::vec2 cornersScreen[4];
    };

    /**
     * @brief One rotation ring, sampled into the points it is both drawn and tested with
     */
    struct RingHandle {
        Axis axis;
        glm::vec3 normal;
        std::vector<glm::vec3> points;
        std::vector<glm::vec2> pointsScreen;
        std::vector<uint8_t> pointsFacing; ///< whether each point is on the half turned toward the camera
    };

  private:
    void buildHandles(const Params &params);
    void buildAxes();
    void buildPlanes();
    void buildRings();

    /**
     * @brief The handle nearest the cursor within the pick radius
     * @param params What the cursor is doing
     * @return The handle's axis, or AXIS_NONE where nothing is close enough
     */
    Axis hitTest(const Params &params) const;

    /**
     * @brief Show the value the active drag has reached, or hide it while nothing is dragged
     * @param params What the cursor is doing
     */
    void updateValueLabel(const Params &params);

    void submitShapes(const Params &params, Rapture::ImmediateDrawList &drawList) const;
    void submitTranslate(Rapture::ShapeSubmission &submission) const;
    void submitPlanes(Rapture::ShapeSubmission &submission) const;
    void submitRotate(Rapture::ShapeSubmission &submission) const;
    void submitScale(Rapture::ShapeSubmission &submission) const;

    /**
     * @brief Take the constraint a drag will be solved against for its whole duration
     * @param params What the cursor is doing as the handle is grabbed
     */
    void beginDrag(const Params &params);

    glm::vec3 solveTranslate(const Params &params) const;
    float solveRotate(const Params &params);
    glm::vec3 solveScale(const Params &params) const;

    /**
     * @brief Where a world position lands in viewport pixels
     * @param world The position
     * @return The pixel, y down
     */
    glm::vec2 toScreen(const glm::vec3 &world) const;

    /**
     * @brief The ray through a viewport pixel
     * @param screen The pixel, y down
     * @param[out] origin Where the ray starts
     * @param[out] direction Which way it points, normalized
     */
    void screenRay(const glm::vec2 &screen, glm::vec3 &origin, glm::vec3 &direction) const;

  private:
    Amethyst::TextLabel *m_valueLabel = nullptr;

    std::vector<AxisHandle> m_axes;
    std::vector<PlaneHandle> m_planes;
    std::vector<RingHandle> m_rings;

    glm::mat4 m_viewProj{1.0f};
    glm::mat4 m_invViewProj{1.0f};
    glm::vec2 m_viewportSize{0.0f};
    glm::vec3 m_center{0.0f};
    glm::vec3 m_basis[3]{};
    glm::vec3 m_toCamera{0.0f, 0.0f, 1.0f};
    float m_worldScale = 1.0f;

    Axis m_hoveredAxis = AXIS_NONE;
    Axis m_activeAxis = AXIS_NONE;
    Operation m_activeOperation = OPERATION_TRANSLATE;

    // The frame the active handle was grabbed in. Solving against the live one instead would move
    // what the drag is measured from as the drag moves the object, and feed back into itself
    glm::vec3 m_dragCenter{0.0f};
    glm::vec3 m_dragBasis[3]{};

    glm::vec3 m_dragStartHit{0.0f};
    glm::vec3 m_dragConstraintDirection{0.0f};
    glm::vec3 m_dragConstraintNormal{0.0f};
    float m_dragStartAngle = 0.0f;
    float m_dragStartDistance = 0.0f;

    // A drag reports what changed since the last frame, so what has already been handed over is kept
    // and the quantised total is differenced against it
    glm::vec3 m_appliedTranslate{0.0f};
    glm::vec3 m_appliedScale{1.0f};
    float m_appliedRotation = 0.0f;
};

} // namespace gizmo

#endif // RAPTURE__TRANSFORM_GIZMO_H
