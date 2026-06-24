#ifndef RAPTURE__INPUT_H
#define RAPTURE__INPUT_H

#include "input/InputCodes.h"
#include "window_context/WindowContext.h"

#include <glm/glm.hpp>

namespace Rapture {

/**
 * @brief Polls one window's keyboard and mouse and exposes per-frame motion.
 */
class Input {
  public:
    /**
     * @brief Construct an input bound to a window.
     * @param window Window to poll; must outlive this input.
     */
    explicit Input(WindowContext *window);
    virtual ~Input();

    /**
     * @brief Test whether a key is currently held down.
     * @param key Key to query.
     * @return True if the key is pressed.
     */
    virtual bool isKeyPressed(KeyCode key) const;

    /**
     * @brief Test whether a mouse button is currently held down.
     * @param button Button to query.
     * @return True if the button is pressed.
     */
    virtual bool isMouseButtonPressed(MouseButton button) const;

    /**
     * @brief Get the current cursor position.
     * @return Cursor position in window pixels.
     */
    glm::vec2 mousePosition() const;

    /**
     * @brief Get cursor movement since the previous frame.
     * @return Delta in pixels; x is right positive, y is down positive.
     */
    glm::vec2 mouseDelta() const { return m_mouseDelta; }

    /**
     * @brief Get scroll wheel movement accumulated this frame.
     * @return Vertical scroll delta.
     */
    float scrollDelta() const { return m_scrollDelta; }

    /**
     * @brief Set the cursor mode and swallow the resulting motion jump.
     * @param mode Cursor mode to apply.
     */
    void setCursorMode(CursorMode mode);

    /**
     * @brief Get the current cursor mode.
     * @return The active cursor mode.
     */
    CursorMode getCursorMode() const;

    /**
     * @brief Advance per-frame state: recompute mouse delta and latch scroll.
     */
    virtual void onUpdate();

  protected:
    WindowContext *m_window = nullptr;
    glm::vec2 m_lastMousePos = {0.0f, 0.0f};
    glm::vec2 m_mouseDelta = {0.0f, 0.0f};
    float m_scrollDelta = 0.0f;
    bool m_skipNextDelta = true;
};

} // namespace Rapture

#endif // RAPTURE__INPUT_H
