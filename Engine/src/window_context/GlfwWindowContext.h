#pragma once

#include "../WindowContext.h"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif

#include <GLFW/glfw3native.h>

namespace Rapture {

class GlfwWindowContext : public WindowContext {
  public:
    GlfwWindowContext(int width, int height, const char *title);
    ~GlfwWindowContext();

    void initWindow() override;
    void closeWindow() override;
    void onUpdate() override;

    void *getNativeWindowContext() override;
    void getFramebufferSize(int *width, int *height) const override;
    void waitEvents() const override;

    const char **getExtensions() override;
    uint32_t getExtensionCount() override;

    // GLFW static callback functions
    static void errorCallback(int error, const char *description);

    static void windowCloseCallback(GLFWwindow *window);
    static void windowSizeCallback(GLFWwindow *window, int width, int height);
    static void windowFocusCallback(GLFWwindow *window, int focused);
    static void windowIconifyCallback(GLFWwindow *window, int iconified);
    static void windowMaximizeCallback(GLFWwindow *window, int maximized);

    static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow *window, unsigned int codepoint);
    static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);

    // Add other callbacks as needed (e.g., joystick, monitor)

  private:
    GLFWwindow *m_glfwWindow;
    const char *m_title;
};

} // namespace Rapture