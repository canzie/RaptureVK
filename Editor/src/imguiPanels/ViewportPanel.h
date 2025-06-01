#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
//#include "../vendor/ImGuizmo/ImGuizmo.h"
#include <functional>
#include <memory>




class ViewportPanel {
public:
    ViewportPanel();
    ~ViewportPanel() = default;

    void renderSceneViewport(ImTextureID textureID);

    
private:


};

