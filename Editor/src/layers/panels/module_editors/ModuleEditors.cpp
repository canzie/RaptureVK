#include "ModuleEditors.h"

void CameraControllerEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Movement Speed", &m_movementSpeed, 0.1f, 100.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->movementSpeed = v;
            }
        });
        rowSlider(t, "Mouse Sensitivity", &m_mouseSensitivity, 0.01f, 1.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->mouseSensitivity = v;
            }
        });
        rowSlider(t, "Orbit Sensitivity", &m_orbitSensitivity, 0.01f, 2.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->orbitSensitivity = v;
            }
        });
        rowSlider(
            t, "Pan Speed", &m_panSpeed, 0.0001f, 0.01f,
            [this](float v) {
                if (m_controller != nullptr) {
                    m_controller->panSpeed = v;
                }
            },
            "%.4f");
        rowSlider(t, "Zoom Speed", &m_zoomSpeed, 0.01f, 1.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->zoomSpeed = v;
            }
        });
        rowSlider(
            t, "Max Pitch", &m_maxPitch, 0.0f, 89.9f,
            [this](float v) {
                if (m_controller != nullptr) {
                    m_controller->maxPitch = v;
                }
            },
            "%.1f deg");
    });
}

void CameraControllerEditor::sync(Rapture::ModuleClass *module)
{
    m_controller = module != nullptr ? module->as<Rapture::CameraController>() : nullptr;
    if (m_controller == nullptr) {
        return;
    }

    m_mouseSensitivity = m_controller->mouseSensitivity;
    m_movementSpeed = m_controller->movementSpeed;
    m_orbitSensitivity = m_controller->orbitSensitivity;
    m_panSpeed = m_controller->panSpeed;
    m_zoomSpeed = m_controller->zoomSpeed;
    m_maxPitch = m_controller->maxPitch;
}
