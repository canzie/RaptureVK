#pragma once

#include <imgui.h>

#include "Components/Components.h"
#include "Components/FogComponent.h"
#include "Components/IndirectLightingComponent.h"
#include "Components/TerrainComponent.h"
#include "Scenes/Entities/Entity.h"
#include "imguiPanels/modules/ScratchBuffer.h"

#include "imgui_impl_vulkan.h"
#include <cstring>
#include <memory>
#include <string>

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
    void renderLightComponent(Rapture::LightComponent &lightComp);
    void renderCameraComponent(Rapture::CameraComponent &cameraComp);
    void renderShadowComponent(Rapture::ShadowComponent &shadowComp);
    void renderCascadedShadowComponent(Rapture::CascadedShadowComponent &csmComp);
    void renderMeshComponent(Rapture::MeshComponent &MeshComponent);
    void renderFogComponent(Rapture::FogComponent &fogComp);
    void renderIndirectLightingComponent(Rapture::IndirectLightingComponent &ilComp);
    void renderSkyboxComponent(Rapture::SkyboxComponent &skyboxComp);
    void renderTerrainComponent(Rapture::TerrainComponent &terrainComp);

    void renderAddComponentMenu(Rapture::Entity entity);

  private:
    std::weak_ptr<Rapture::Entity> m_selectedEntity;
    char m_searchFilter[256] = "";

    VkDescriptorSet m_currentShadowMapDescriptorSet;
    VkDescriptorSet m_currentCSMDescriptorSet;

    size_t m_entitySelectedListenerId;

    ScratchBuffer componentTmpStorage;

    struct TerrainTextureCache {
        Rapture::AssetHandle cachedHandles[Rapture::TERRAIN_NC_COUNT] = {};
        static constexpr int MAX_VISIBLE = 25;

        void clear()
        {
            for (uint8_t i = 0; i < Rapture::TERRAIN_NC_COUNT; ++i) {
                cachedHandles[i] = 0;
            }
        }
    };
    TerrainTextureCache m_terrainTextureCache;
};
