#ifndef RAPTURE__MODULE_EDITORS_H
#define RAPTURE__MODULE_EDITORS_H

#include "Icons.h"
#include "ModuleEditorBase.h"
#include "modules/controllers/CameraController.h"

class CameraControllerEditor : public ModuleEditorBase {
  public:
    const char *title() const override { return "Camera Controller"; }
    const char *icon() const override { return Icons::SVG_CAMERA; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync(Rapture::ModuleClass *module) override;

  private:
    float m_mouseSensitivity = 0.1f;
    float m_movementSpeed = 5.0f;
    float m_orbitSensitivity = 0.3f;
    float m_panSpeed = 0.0015f;
    float m_zoomSpeed = 0.15f;
    float m_maxPitch = 89.0f;

    Rapture::CameraController *m_controller = nullptr;
};

#endif // RAPTURE__MODULE_EDITORS_H
