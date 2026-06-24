#pragma once

#include "input/InputCodes.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Rapture {

// Buffer swap mode enumeration
enum class SwapMode {
    Immediate,      // No VSync, uncapped framerate (double buffering)
    VSync,          // Traditional VSync with double buffering
    AdaptiveVSync,  // Adaptive VSync with triple buffering (if supported)
    TripleBuffering // Triple buffering without VSync (uncapped framerate)
};

enum class CursorMode {
    NORMAL,   // Cursor visible and free to move
    HIDDEN,   // Cursor hidden but still free to move within the window
    DISABLED  // Cursor hidden and locked to the window, reports unbounded relative motion
};

class WindowContext {

  public:
    virtual ~WindowContext() {};

    // create context and set the callbacks
    virtual void initWindow(void) = 0;
    virtual void closeWindow(void) = 0;

    virtual void onUpdate(void) = 0;

    virtual void *getNativeWindowContext() = 0;

    virtual void getFramebufferSize(int *width, int *height) const = 0;

    bool isMinimized() const { return m_minimized; }

    virtual void waitEvents() const = 0;

    virtual const char **getExtensions() = 0;
    virtual uint32_t getExtensionCount() = 0;

    // Buffer swap control functions (implemented by derived classes)
    virtual void setSwapMode(SwapMode mode) { m_swapMode = mode; }
    virtual SwapMode getSwapMode() const { return m_swapMode; }
    virtual bool isTripleBufferingSupported() const { return false; }

    /**
     * @brief Set how the cursor behaves over this window.
     * @param mode The cursor mode to apply.
     */
    virtual void setCursorMode(CursorMode mode) = 0;

    /**
     * @brief Get the current cursor mode.
     * @return The active cursor mode.
     */
    virtual CursorMode getCursorMode() const = 0;

    /**
     * @brief Test whether a key is currently held down.
     * @param key Key to query.
     * @return True if the key is pressed.
     */
    virtual bool isKeyPressed(KeyCode key) const = 0;

    /**
     * @brief Test whether a mouse button is currently held down.
     * @param button Button to query.
     * @return True if the button is pressed.
     */
    virtual bool isMouseButtonPressed(MouseButton button) const = 0;

    /**
     * @brief Get the current cursor position.
     * @return Cursor position in window pixels.
     */
    virtual glm::vec2 getCursorPosition() const = 0;

    static WindowContext *createWindow(int width, int height, const char *title);

  protected:
    struct ContextData {
        int height;
        int width;
    } m_context_data;

    bool m_minimized = false;

  private:
    SwapMode m_swapMode = SwapMode::Immediate;
};

} // namespace Rapture
