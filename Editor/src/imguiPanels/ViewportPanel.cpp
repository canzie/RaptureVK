#include "ViewportPanel.h"


ViewportPanel::ViewportPanel()
{

}


void ViewportPanel::renderSceneViewport(ImTextureID textureID)
{

    ImGui::Begin("Viewport");

    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    ImGui::Image((ImTextureID)textureID, windowSize);

    ImGui::End();
}