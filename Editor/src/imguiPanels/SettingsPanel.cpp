#include "SettingsPanel.h"

#include "Renderer/DeferredShading/DeferredRenderer.h"
#include "Renderer/DeferredShading/LightingPass.h"

SettingsPanel::SettingsPanel() {

}

SettingsPanel::~SettingsPanel() {

}

void SettingsPanel::render() {
    ImGui::Begin("Settings");

    renderRendererSettings();


    ImGui::End();

    
}

void SettingsPanel::renderRendererSettings() {
    ImGui::Separator();
    renderDDGISettings();
    ImGui::Separator();
    renderFogSettings();
}

void SettingsPanel::renderDDGISettings() {

    ImGui::Text("DDGI Settings");

    auto ddgi = Rapture::DeferredRenderer::getDynamicDiffuseGI();
    auto& probeVolume = ddgi->getProbeVolume();

    if (ImGui::SliderFloat("Hysteresis", &probeVolume.probeHysteresis, 0.0f, 1.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Max Ray Distance", &probeVolume.probeMaxRayDistance, 1000.0f, 100000000.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Normal Bias", &probeVolume.probeNormalBias, 0.0f, 5.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }   

    if (ImGui::SliderFloat("Probe View Bias", &probeVolume.probeViewBias, 0.0f, 5.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Distance Exponent", &probeVolume.probeDistanceExponent, 0.0f, 100.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Irradiance Encoding Gamma", &probeVolume.probeIrradianceEncodingGamma, 0.0f, 10.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Brightness Threshold", &probeVolume.probeBrightnessThreshold, 0.0f, 1.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat3("Probe Spacing", &probeVolume.spacing.x, 0.1f, 10.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }




}

void SettingsPanel::renderFogSettings() {
    ImGui::Text("Fog Settings");

    auto lightingPass = Rapture::DeferredRenderer::getLightingPass();
    if (lightingPass) {
        auto& fogSettings = lightingPass->getFogSettings();

        ImGui::Checkbox("Enabled", &fogSettings.enabled);
        ImGui::ColorEdit3("Fog Color", &fogSettings.color.x);
        ImGui::DragFloat("Near Distance", &fogSettings.nearDistance, 0.1f, 0.0f, fogSettings.farDistance, "%.2f");
        ImGui::DragFloat("Far Distance", &fogSettings.farDistance, 0.1f, fogSettings.nearDistance, 1000.0, "%.2f");
    }
}
