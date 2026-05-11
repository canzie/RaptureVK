#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <map>

namespace Rapture {

    // Forward declaration
    struct TransformComponent;
    struct CameraComponent;

    class CameraController {
    public:

        CameraController();
        ~CameraController();

        // Main update method - handles input and updates camera
        void update(float deltaTime, TransformComponent& transform, CameraComponent& camera);

        // Input sensitivity settings
        float mouseSensitivity = 0.1f;
        float movementSpeed = 5.0f;
        
        // Controller state
        float yaw = -90.0f;
        float pitch = 0.0f;
        bool constrainPitch = true;
        float maxPitch = 89.0f;

        glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);

    private:        

        glm::vec2 m_lastMousePos = {0.0f, 0.0f};
        glm::vec2 m_mouseOffset = {0.0f, 0.0f};

        bool s_isMouseLocked = true;

        
        // Input handling methods
        void handleKeyboardInput(float ts, TransformComponent& transform);
        void handleMouseInput(float ts);

        size_t mouseListenerID;
        size_t keyboardPressedListenerID;
        size_t keyboardReleasedListenerID;

        std::map<int, bool> m_keysPressed;
        


    };

} 