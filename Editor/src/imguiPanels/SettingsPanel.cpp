#include "SettingsPanel.h"

#include "Renderer/DeferredShading/DeferredRenderer.h"
#include "Renderer/DeferredShading/LightingPass.h"
#include "imguiPanels/modules/BetterPrimitives.h"

SettingsPanel::SettingsPanel() {}

SettingsPanel::~SettingsPanel() {}

void SettingsPanel::render()
{
    if (!BetterUi::BeginPanel("Settings")) {
        BetterUi::EndPanel();
        return;
    }

    BetterUi::BeginContent();
    renderRendererSettings();
    BetterUi::EndContent();
    BetterUi::EndPanel();
}

void SettingsPanel::renderRendererSettings()
{
    ImGui::Text("Renderer Settings");
    ImGui::Separator();
}
