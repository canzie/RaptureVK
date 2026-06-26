#pragma once

#include "input/InputCodes.h"
#include "window_context/PlatformContext.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Rapture {

constexpr uint32_t WINDOW_CTX_ID_INVALID = 0;

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
    WindowContext() : m_id(s_nextId++) {}
    virtual ~WindowContext() {};

    // create context and set the callbacks
    virtual void initWindow(void) = 0;
    virtual void closeWindow(void) = 0;

    virtual void onUpdate(void) = 0;

    virtual void *getNativeWindowContext() = 0;

    virtual void getFramebufferSize(int *width, int *height) const = 0;

    /**
     * @brief Check whether the native window has received a close request.
     * @return True if the window should be closed.
     */
    virtual bool shouldClose() const = 0;

    /**
     * @brief Ask the native window to close, as if the user clicked its close button.
     */
    virtual void requestClose() = 0;

    uint32_t getId() const { return m_id; }

    bool isMinimized() const { return m_minimized; }

    /**
     * @brief Return scroll accumulated since the last call and reset it.
     * @return Vertical scroll delta.
     */
    float consumeScrollDelta()
    {
        float delta = m_scrollAccumulator;
        m_scrollAccumulator = 0.0f;
        return delta;
    }

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

    /**
     * @brief Create a new platform window.
     * @param platform Shared platform context; must outlive this window.
     * @param width Initial width.
     * @param height Initial height.
     * @param title Window title.
     * @param preferFloating Hint to tiling window managers that this window should float
     * rather than tile. No effect on non-tiling platforms.
     */
    static WindowContext *createWindow(PlatformContext &platform, int width, int height, const char *title,
                                       bool preferFloating = false);

  protected:
    struct ContextData {
        int height;
        int width;
    } m_context_data;

    bool m_minimized = false;
    float m_scrollAccumulator = 0.0f;

  private:
    static uint32_t s_nextId;

    uint32_t m_id;
    SwapMode m_swapMode = SwapMode::Immediate;
};

} // namespace Rapture
