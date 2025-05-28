#pragma once

#include "Layers/Layer.h"

//#include "Renderer/ForwardRenderer/ForwardRenderer.h"

#include "imgui.h"

#include "imgui_impl_glfw.h"

#include "imgui_impl_vulkan.h"
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class ImGuiLayer : public Rapture::Layer
{
public:
    ImGuiLayer();
    ~ImGuiLayer() = default;
    
    virtual void onAttach() override;
    virtual void onDetach() override;
    virtual void onUpdate(float ts) override;

private:

private:
    float m_Time = 0.0f;
    float m_FontScale = 1.5f; // Default font scale


    VkDescriptorPool m_imguiPool;

};