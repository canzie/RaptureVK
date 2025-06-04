#include "ViewportPanel.h"
#include "Logging/TracyProfiler.h"

ViewportPanel::ViewportPanel()
{

}


void ViewportPanel::renderSceneViewport(ImTextureID textureID)
{
    RAPTURE_PROFILE_FUNCTION();

    ImGui::Begin("Viewport");

    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    ImGui::Image((ImTextureID)textureID, windowSize);

    ImGui::End();
}