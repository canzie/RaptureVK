#pragma once

#include <imgui.h>

#include "Scenes/Entities/Entity.h"
#include "imgui_impl_vulkan.h"
#include <memory>

class PropertiesPanel {
  public:
    PropertiesPanel();
    ~PropertiesPanel();

    void render();

    // Helper function to display a help marker with tooltip
    static void HelpMarker(const char *desc);

  private:
    void renderMaterialComponent();
    void renderTransformComponent();
    void renderLightComponent();
    void renderCameraComponent();
    void renderShadowComponent();
    void renderCascadedShadowComponent();
    void renderMeshComponent();
    void renderRigidBodyComponent();
    void renderFogComponent();
    void renderIndirectLightingComponent();

    void renderAddComponentMenu(Rapture::Entity entity);

  private:
    std::weak_ptr<Rapture::Entity> m_selectedEntity;

    VkDescriptorSet m_currentShadowMapDescriptorSet;
    VkDescriptorSet m_currentCSMDescriptorSet;

    size_t m_entitySelectedListenerId;
};
