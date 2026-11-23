#ifndef RAPTURE__ORIENTATION_GIZMO_H
#define RAPTURE__ORIENTATION_GIZMO_H

#include <amethyst/Amethyst.h>
#include <components/canvas.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>
#include <functional>

/**
 * @brief The world axes as seen through a camera, each end clickable to look along that axis
 */
class OrientationGizmo : public Amethyst::Canvas {
  public:
    OrientationGizmo();

    /**
     * @brief Shows the axes as seen through a camera
     * @param viewRotation The world rotation of the camera
     */
    void setViewRotation(const glm::quat &viewRotation);

  public:
    /// Called with the direction to look along when an axis end is clicked
    std::function<void(const glm::vec3 &)> onAxisEndClicked;

  protected:
    Amethyst::EventResult onInputBegan(const Amethyst::InputObject &input) override;
    Amethyst::EventResult onMouseMoved(Amethyst::vec2 position) override;
    Amethyst::EventResult onMouseLeave(void) override;

  private:
    struct AxisEnd {
        glm::vec3 direction;
        Amethyst::vec2 center;
        float depth;
        uint8_t axis;
        bool positive;
    };

    static constexpr int32_t NO_AXIS_END = -1;

    void redraw(void);
    int32_t findAxisEndAt(Amethyst::vec2 localPosition) const;
    void setHoveredAxisEnd(int32_t axisEnd);

  private:
    std::array<AxisEnd, 6> m_axisEnds{};
    glm::quat m_viewRotation{1.0f, 0.0f, 0.0f, 0.0f};
    int32_t m_hoveredAxisEnd = NO_AXIS_END;
};

#endif // RAPTURE__ORIENTATION_GIZMO_H
