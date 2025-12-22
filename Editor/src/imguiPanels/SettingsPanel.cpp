#include "SettingsPanel.h"

#include "Renderer/DeferredShading/DeferredRenderer.h"
#include "Renderer/DeferredShading/LightingPass.h"

SettingsPanel::SettingsPanel() {}

SettingsPanel::~SettingsPanel() {}

void SettingsPanel::render()
{
    ImGui::Begin("Settings");

    renderRendererSettings();

    ImGui::End();
}

void SettingsPanel::renderRendererSettings()
{
    ImGui::Text("Renderer Settings");
    ImGui::Separator();
}
