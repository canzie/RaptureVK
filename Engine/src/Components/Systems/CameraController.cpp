#include "CameraController.h"
#include "Components/Components.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Events/InputEvents.h"
#include "Input/Keybinds.h"


Rapture::CameraController::CameraController()
{
    mouseListenerID = InputEvents::onMouseMoved().addListener([this](float x, float y) {
        
        m_mouseOffset = glm::vec2(x-m_lastMousePos.x, m_lastMousePos.y-y);
        m_lastMousePos = glm::vec2(x, y);

    });

    keyboardPressedListenerID = InputEvents::onKeyPressed().addListener([this](int key, int repeat) {
        m_keysPressed[key] = true;
    });

    keyboardReleasedListenerID = InputEvents::onKeyReleased().addListener([this](int key) {
        m_keysPressed[key] = false;
    });

}

Rapture::CameraController::~CameraController()
{
    InputEvents::onMouseMoved().removeListener(mouseListenerID);
    InputEvents::onKeyPressed().removeListener(keyboardPressedListenerID);
    InputEvents::onKeyReleased().removeListener(keyboardReleasedListenerID);
}

void Rapture::CameraController::update(float ts, TransformComponent& transform, CameraComponent& camera)
{
    // Ensure time is in seconds
    float deltaTime = ts;
    if (deltaTime > 0.1f) { // Likely in milliseconds
        deltaTime *= 0.001f; // Convert to seconds
    }
    
    // Clamp delta time to avoid huge jumps when framerate drops very low
    if (deltaTime > 0.1f) {
        deltaTime = 0.1f;
    }
    
    if (!s_isMouseLocked)
    {
        handleMouseInput(deltaTime);
    }

    handleKeyboardInput(deltaTime, transform);
    
    // Update camera view matrix
    camera.updateViewMatrix(transform, cameraFront);
    
}

void Rapture::CameraController::handleMouseInput(float deltaTime)
{


    // Mouse sensitivity is now in degrees per second
    m_mouseOffset *= mouseSensitivity;

    yaw += m_mouseOffset.x;
    pitch += m_mouseOffset.y;

    // Clamp pitch to avoid flipping
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    // Update camera front direction
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void Rapture::CameraController::handleKeyboardInput(float deltaTime, TransformComponent& transform)
{
    
    // Calculate right vector for strafing
    glm::vec3 right = glm::normalize(glm::vec3(cameraFront.z, 0.0f, -cameraFront.x));


    // Calculate actual movement distance based on speed and delta time
    float moveDistance = movementSpeed * deltaTime;
    glm::vec3 currentTranslation = transform.translation();
    // Handle movement keys - using separate if statements allows for diagonal movement
    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MoveLeft)])
    {
        currentTranslation += moveDistance * right;
    }
    
    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MoveRight)])
    {
        currentTranslation -= moveDistance * right;
    }
    
    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MoveForward)])
    {
        currentTranslation += moveDistance * cameraFront;
    }
    
    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MoveBackward)])
    {
        currentTranslation -= moveDistance * cameraFront;
    }
    
    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MoveUp)])
    {
        currentTranslation.y += moveDistance;
    }
    
    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MoveDown)])
    {
        currentTranslation.y -= moveDistance;
    }

    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MouseLock)])
    {
        s_isMouseLocked = true;
    }

    if (m_keysPressed[static_cast<int>(Rapture::KeyAction::MouseUnlock)])
    {
        s_isMouseLocked = false;
    }
    
    transform.transforms.setTranslation(currentTranslation);

} 