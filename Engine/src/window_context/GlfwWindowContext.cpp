#include "GlfwWindowContext.h"
#include "events/InputEvents.h"
#include "logging/Log.h"
#include "utils/rp_assert.h"

namespace Rapture {

uint32_t WindowContext::s_nextId = 1;

GlfwWindowContext::GlfwWindowContext(PlatformContext &platform, int width, int height, const char *title, bool preferFloating)
    : m_glfwWindow(nullptr), m_title(title), m_preferFloating(preferFloating)
{
    (void)platform; // GLFW is already initialized by the time any window is constructed
    m_context_data.width = width;
    m_context_data.height = height;
    initWindow();
}

GlfwWindowContext::~GlfwWindowContext()
{
    closeWindow();
}

void GlfwWindowContext::initWindow()
{
    RP_CORE_INFO("========== Initializing GLFW Window Context ==========");

    glfwSetErrorCallback(GlfwWindowContext::errorCallback);

    // For Vulkan, you'll need to tell GLFW not to create an OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_glfwWindow = glfwCreateWindow(m_context_data.width, m_context_data.height, m_title, nullptr, nullptr);
    RP_ASSERT(m_glfwWindow != nullptr, "Failed to create GLFW window!");
    if (!m_glfwWindow) {
        return;
    }

    glfwSetWindowUserPointer(m_glfwWindow, this);

    // Set GLFW Callbacks
    glfwSetWindowCloseCallback(m_glfwWindow, GlfwWindowContext::windowCloseCallback);
    glfwSetWindowSizeCallback(m_glfwWindow, GlfwWindowContext::windowSizeCallback);
    glfwSetKeyCallback(m_glfwWindow, GlfwWindowContext::keyCallback);
    glfwSetCharCallback(m_glfwWindow, GlfwWindowContext::charCallback);
    glfwSetMouseButtonCallback(m_glfwWindow, GlfwWindowContext::mouseButtonCallback);
    glfwSetCursorPosCallback(m_glfwWindow, GlfwWindowContext::cursorPosCallback);
    glfwSetScrollCallback(m_glfwWindow, GlfwWindowContext::scrollCallback);
    glfwSetWindowFocusCallback(m_glfwWindow, GlfwWindowContext::windowFocusCallback);
    glfwSetWindowIconifyCallback(m_glfwWindow, GlfwWindowContext::windowIconifyCallback);
    // glfwSetWindowMaximizeCallback(m_glfwWindow, GlfwWindowContext::windowMaximizeCallback);

    RP_CORE_INFO("========== GLFW Window Context Initialized Successfully. ==========");
}

void GlfwWindowContext::closeWindow()
{
    if (m_glfwWindow) {
        glfwDestroyWindow(m_glfwWindow);
        m_glfwWindow = nullptr;
    }
    RP_CORE_INFO("========== GLFW Window Context Closed. ==========");
}

void GlfwWindowContext::onUpdate()
{
    glfwPollEvents();
    // Swapping buffers will be handled by the Vulkan renderer, not directly here usually
}

void *GlfwWindowContext::getNativeWindowContext()
{
    return m_glfwWindow;
}

void GlfwWindowContext::getFramebufferSize(int *width, int *height) const
{
    glfwGetFramebufferSize(m_glfwWindow, width, height);
}

bool GlfwWindowContext::shouldClose() const
{
    return glfwWindowShouldClose(m_glfwWindow) != 0;
}

void GlfwWindowContext::requestClose()
{
    glfwSetWindowShouldClose(m_glfwWindow, GLFW_TRUE);
}

void GlfwWindowContext::waitEvents() const
{
    glfwWaitEvents();
}

void GlfwWindowContext::setCursorMode(CursorMode mode)
{
    m_cursorMode = mode;

    int glfwMode = GLFW_CURSOR_NORMAL;
    switch (mode) {
    case CursorMode::NORMAL:
        glfwMode = GLFW_CURSOR_NORMAL;
        break;
    case CursorMode::HIDDEN:
        glfwMode = GLFW_CURSOR_HIDDEN;
        break;
    case CursorMode::DISABLED:
        glfwMode = GLFW_CURSOR_DISABLED;
        break;
    }
    glfwSetInputMode(m_glfwWindow, GLFW_CURSOR, glfwMode);

    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(m_glfwWindow, GLFW_RAW_MOUSE_MOTION, mode == CursorMode::DISABLED ? GLFW_TRUE : GLFW_FALSE);
    }
}

CursorMode GlfwWindowContext::getCursorMode() const
{
    return m_cursorMode;
}

bool GlfwWindowContext::isKeyPressed(KeyCode key) const
{
    return glfwGetKey(m_glfwWindow, key) == GLFW_PRESS;
}

bool GlfwWindowContext::isMouseButtonPressed(MouseButton button) const
{
    return glfwGetMouseButton(m_glfwWindow, button) == GLFW_PRESS;
}

glm::vec2 GlfwWindowContext::getCursorPosition() const
{
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(m_glfwWindow, &x, &y);
    return glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

const char **GlfwWindowContext::getExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    return glfwExtensions;
}

uint32_t GlfwWindowContext::getExtensionCount()
{
    uint32_t glfwExtensionCount = 0;
    glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    return glfwExtensionCount;
}

// GLFW Static Callback Implementations

void GlfwWindowContext::errorCallback(int error, const char *description)
{
    RP_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
}

void GlfwWindowContext::windowCloseCallback(GLFWwindow *window)
{
    GlfwWindowContext *context = static_cast<GlfwWindowContext *>(glfwGetWindowUserPointer(window));
    if (context == nullptr) {
        return;
    }
    context->onClose.fire();
}

void GlfwWindowContext::windowSizeCallback(GLFWwindow *window, int width, int height)
{
    GlfwWindowContext *context = static_cast<GlfwWindowContext *>(glfwGetWindowUserPointer(window));
    if (context == nullptr || context->getId() == WINDOW_CTX_ID_INVALID) {
        return;
    }
    context->m_context_data.width = width;
    context->m_context_data.height = height;
    context->onResize.fire(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void GlfwWindowContext::keyCallback(GLFWwindow *window, int key, int /*scancode*/, int action, int /*mods*/)
{
    (void)window;
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    switch (action) {
    case GLFW_PRESS:
        InputEvents::onKeyPressed().publish(key, 0); // GLFW doesn't directly give repeat count here easily, need to track
        break;
    case GLFW_RELEASE:
        InputEvents::onKeyReleased().publish(key);
        break;
    case GLFW_REPEAT: // GLFW_REPEAT can be handled as another KeyPressed event if your system expects it
        InputEvents::onKeyPressed().publish(key, 1); // Or manage repeat count if your event takes it.
        break;
    }
}

void GlfwWindowContext::charCallback(GLFWwindow *window, unsigned int codepoint)
{
    (void)window;
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    InputEvents::onKeyTyped().publish(codepoint);
}

void GlfwWindowContext::mouseButtonCallback(GLFWwindow *window, int button, int action, int /*mods*/)
{
    (void)window;
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    switch (action) {
    case GLFW_PRESS:
        InputEvents::onMouseButtonPressed().publish(button);
        break;
    case GLFW_RELEASE:
        InputEvents::onMouseButtonReleased().publish(button);
        break;
    }
}

void GlfwWindowContext::cursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
    (void)window;
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    InputEvents::onMouseMoved().publish(static_cast<float>(xpos), static_cast<float>(ypos));
}

void GlfwWindowContext::scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    (void)xoffset;
    GlfwWindowContext *context = static_cast<GlfwWindowContext *>(glfwGetWindowUserPointer(window));
    if (context != nullptr) {
        context->m_scrollAccumulator += static_cast<float>(yoffset);
    }
}

void GlfwWindowContext::windowFocusCallback(GLFWwindow *window, int focused)
{
    GlfwWindowContext *context = static_cast<GlfwWindowContext *>(glfwGetWindowUserPointer(window));
    if (context == nullptr) {
        return;
    }
    context->onFocus.fire(focused != 0);
}

void GlfwWindowContext::windowIconifyCallback(GLFWwindow *window, int iconified)
{
    GlfwWindowContext *context = static_cast<GlfwWindowContext *>(glfwGetWindowUserPointer(window));
    if (context != nullptr) {
        context->m_minimized = (iconified != 0);
    }
}

// Implement windowMaximizeCallback if needed
// void GlfwWindowContext::windowMaximizeCallback(GLFWwindow* window, int maximized) { ... }

WindowContext *WindowContext::createWindow(PlatformContext &platform, int width, int height, const char *title, bool preferFloating)
{
    return new GlfwWindowContext(platform, width, height, title, preferFloating);
}

} // namespace Rapture
