#pragma once

#include <imgui.h>

#include "Scenes/Entities/Entity.h"
#include <memory>
#include "imgui_impl_vulkan.h"

class PropertiesPanel {
public:
    PropertiesPanel();
    ~PropertiesPanel();

    void render();

    // Helper function to display a help marker with tooltip
    static void HelpMarker(const char* desc);

private:
    void renderMaterialComponent();
    void renderTransformComponent();
    void renderLightComponent();
    void renderCameraComponent();
    void renderShadowComponent();
    void renderCascadedShadowComponent();

private:

    std::weak_ptr<Rapture::Entity> m_selectedEntity;

    VkDescriptorSet m_currentShadowMapDescriptorSet;
    VkDescriptorSet m_currentCSMDescriptorSet;

    size_t m_entitySelectedListenerId;
};
