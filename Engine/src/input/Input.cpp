#include "Input.h"

#include "events/InputEvents.h"

namespace Rapture {

Input::Input(WindowContext *window) : m_window(window)
{
    m_scrollListenerId = InputEvents::onMouseScrolled().addListener([this](float xOffset, float yOffset) {
        (void)xOffset;
        m_scrollAccumulator += yOffset;
    });
}

Input::~Input()
{
    InputEvents::onMouseScrolled().removeListener(m_scrollListenerId);
}

bool Input::isKeyPressed(KeyCode key) const
{
    if (m_window == nullptr) {
        return false;
    }
    return m_window->isKeyPressed(key);
}

bool Input::isMouseButtonPressed(MouseButton button) const
{
    if (m_window == nullptr) {
        return false;
    }
    return m_window->isMouseButtonPressed(button);
}

glm::vec2 Input::mousePosition() const
{
    if (m_window == nullptr) {
        return glm::vec2(0.0f);
    }
    return m_window->getCursorPosition();
}

void Input::setCursorMode(CursorMode mode)
{
    if (m_window == nullptr || m_window->getCursorMode() == mode) {
        return;
    }
    m_window->setCursorMode(mode);
    m_skipNextDelta = true;
}

CursorMode Input::getCursorMode() const
{
    if (m_window == nullptr) {
        return CursorMode::NORMAL;
    }
    return m_window->getCursorMode();
}

void Input::onUpdate()
{
    glm::vec2 position = mousePosition();
    if (m_skipNextDelta) {
        m_mouseDelta = glm::vec2(0.0f);
        m_skipNextDelta = false;
    } else {
        m_mouseDelta = position - m_lastMousePos;
    }
    m_lastMousePos = position;

    m_scrollDelta = m_scrollAccumulator;
    m_scrollAccumulator = 0.0f;
}

} // namespace Rapture
