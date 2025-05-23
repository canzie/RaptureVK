#include "GlfwWindowContext.h"
#include "../../Events/ApplicationEvents.h"
#include "../../Events/InputEvents.h"
#include "../../Logging/Log.h" // Assuming you have a logger

#include <stdexcept> // For std::runtime_error

// It's good practice to ensure Events.h is included if it defines EventBus/EventRegistry
// For example, if ApplicationEvents.h or InputEvents.h don't pull it in transitively for some reason.
// #include "../Events/Events.h"

namespace Rapture {

GlfwWindowContext::GlfwWindowContext(int width, int height, const char* title)
    : m_glfwWindow(nullptr), m_title(title) {
    m_context_data.width = width;
    m_context_data.height = height;
    initWindow();
}

GlfwWindowContext::~GlfwWindowContext() {
    closeWindow();
}

void GlfwWindowContext::initWindow() {
    RP_CORE_INFO("========== Initializing GLFW Window Context ==========");

    if (!glfwInit()) {
        RP_CORE_CRITICAL("========== Failed to initialize GLFW! ==========");
        throw std::runtime_error("Failed to initialize GLFW!");
    }

    glfwSetErrorCallback(GlfwWindowContext::errorCallback);

    // For Vulkan, you'll need to tell GLFW not to create an OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_glfwWindow = glfwCreateWindow(m_context_data.width, m_context_data.height, m_title, nullptr, nullptr);
    if (!m_glfwWindow) {
        RP_CORE_CRITICAL("========== Failed to create GLFW window! ==========");
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window!");
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
    // glfwSetWindowIconifyCallback(m_glfwWindow, GlfwWindowContext::windowIconifyCallback);
    // glfwSetWindowMaximizeCallback(m_glfwWindow, GlfwWindowContext::windowMaximizeCallback);

    RP_CORE_INFO("========== GLFW Window Context Initialized Successfully. ==========");
}

void GlfwWindowContext::closeWindow() {
    if (m_glfwWindow) {
        glfwDestroyWindow(m_glfwWindow);
        m_glfwWindow = nullptr;
    }
    glfwTerminate(); // Terminate GLFW when the last window is closed or explicitly
    RP_CORE_INFO("========== GLFW Window Context Closed. ==========");
}

void GlfwWindowContext::onUpdate() {
    glfwPollEvents();
    // Swapping buffers will be handled by the Vulkan renderer, not directly here usually
}

void* GlfwWindowContext::getNativeWindowContext() {
    return m_glfwWindow;
}

void GlfwWindowContext::getFramebufferSize(int *width, int *height) const
{
    glfwGetFramebufferSize(m_glfwWindow, width, height);
}

void GlfwWindowContext::waitEvents() const
{
    glfwWaitEvents();
}

const char **GlfwWindowContext::getExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    return glfwExtensions;
}

uint32_t GlfwWindowContext::getExtensionCount()
{
    uint32_t glfwExtensionCount = 0;
    glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    
    return glfwExtensionCount;
}

// GLFW Static Callback Implementations

void GlfwWindowContext::errorCallback(int error, const char* description) {
    RP_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
}

void GlfwWindowContext::windowCloseCallback(GLFWwindow* window) {
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    // if (context) { // Check if context is valid if necessary }
    ApplicationEvents::onWindowClose().publish();
}

void GlfwWindowContext::windowSizeCallback(GLFWwindow* window, int width, int height) {
    GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    if (context) {
        context->m_context_data.width = width;
        context->m_context_data.height = height;
    }
    ApplicationEvents::onWindowResize().publish(width, height);
}

void GlfwWindowContext::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
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

void GlfwWindowContext::charCallback(GLFWwindow* window, unsigned int codepoint) {
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    InputEvents::onKeyTyped().publish(codepoint);
}

void GlfwWindowContext::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
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

void GlfwWindowContext::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    InputEvents::onMouseMoved().publish(static_cast<float>(xpos), static_cast<float>(ypos));
}

void GlfwWindowContext::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    InputEvents::onMouseScrolled().publish(static_cast<float>(xoffset), static_cast<float>(yoffset));
}

void GlfwWindowContext::windowFocusCallback(GLFWwindow* window, int focused) {
    // GlfwWindowContext* context = static_cast<GlfwWindowContext*>(glfwGetWindowUserPointer(window));
    if (focused) {
        ApplicationEvents::onWindowFocus().publish();
    } else {
        ApplicationEvents::onWindowLostFocus().publish();
    }
}

// Implement windowIconifyCallback and windowMaximizeCallback if needed
// void GlfwWindowContext::windowIconifyCallback(GLFWwindow* window, int iconified) { ... }
// void GlfwWindowContext::windowMaximizeCallback(GLFWwindow* window, int maximized) { ... }

WindowContext* WindowContext::createWindow(int width, int height, const char* title) {
    return new GlfwWindowContext(width, height, title);
}

} // namespace Rapture 