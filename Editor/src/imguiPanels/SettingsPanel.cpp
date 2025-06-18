#include "SettingsPanel.h"

#include "Renderer/DeferredShading/DeferredRenderer.h"

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

}

void SettingsPanel::renderDDGISettings() {

    ImGui::Text("DDGI Settings");

    auto ddgi = Rapture::DeferredRenderer::getDynamicDiffuseGI();
    auto& probeVolume = ddgi->getProbeVolume();

    if (ImGui::SliderFloat("Hysteresis", &probeVolume.probeHysteresis, 0.0f, 1.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Max Ray Distance", &probeVolume.probeMaxRayDistance, 0.0f, 100.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Normal Bias", &probeVolume.probeNormalBias, 0.0f, 1.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }   

    if (ImGui::SliderFloat("Probe View Bias", &probeVolume.probeViewBias, 0.0f, 1.0f)) {
        ddgi->setProbeVolume(probeVolume);
    }

    if (ImGui::SliderFloat("Probe Distance Exponent", &probeVolume.probeDistanceExponent, 0.0f, 10.0f)) {
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
