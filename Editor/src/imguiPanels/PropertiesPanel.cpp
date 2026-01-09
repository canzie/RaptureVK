#include "PropertiesPanel.h"

#include "Components/Components.h"
#include "Components/FogComponent.h"
#include "Components/IndirectLightingComponent.h"
#include "modules/ComponentLayoutRegistry.h"
#include "modules/ComponentLayoutSystem.h"

#include "AssetManager/Asset.h"
#include "AssetManager/AssetManager.h"
#include "Events/GameEvents.h"
#include "Generators/Terrain/TerrainTypes.h"
#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"
#include "Scenes/Entities/Entity.h"
#include "Textures/Texture.h"
#include "imguiPanels/IconsMaterialDesign.h"
#include "imguiPanels/modules/BetterPrimitives.h"

#include "imguiPanels/modules/PlotEditor.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <functional>
#include <memory>

// Implementation of HelpMarker function
void PropertiesPanel::HelpMarker(const char *desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

PropertiesPanel::PropertiesPanel()
{
    m_entitySelectedListenerId = Rapture::GameEvents::onEntitySelected().addListener(
        [this](std::shared_ptr<Rapture::Entity> entity) { m_selectedEntity = entity; });

    m_currentShadowMapDescriptorSet = VK_NULL_HANDLE;
    m_currentCSMDescriptorSet = VK_NULL_HANDLE;
}

PropertiesPanel::~PropertiesPanel()
{
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);

    if (m_currentShadowMapDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_currentShadowMapDescriptorSet);
    }
    if (m_currentCSMDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_currentCSMDescriptorSet);
    }
}

void PropertiesPanel::render()
{
    RAPTURE_PROFILE_FUNCTION();

    componentTmpStorage.reset();

    if (!BetterUi::BeginPanel("Properties")) {
        BetterUi::EndPanel();
        return;
    }

    BetterUi::BeginContent();

    // Searchbar at the top (non-functional for now)
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##PropertiesSearch", ICON_MD_SEARCH " Search...", m_searchFilter, IM_ARRAYSIZE(m_searchFilter));
    ImGui::Separator();

    if (auto entity = m_selectedEntity.lock()) {
        if (entity->hasComponent<Rapture::TransformComponent>()) {
            renderTransformComponent();
        }
        if (entity->hasComponent<Rapture::MaterialComponent>()) {
            renderMaterialComponent();
        }
        if (auto lightComp = entity->tryGetComponent<Rapture::LightComponent>()) {
            renderLightComponent(*lightComp);
        }
        if (auto camComp = entity->tryGetComponent<Rapture::CameraComponent>()) {
            renderCameraComponent(*camComp);
        }
        // Check for shadow component only if entity has both transform and light components
        if (auto shadowComp = entity->tryGetComponent<Rapture::ShadowComponent>()) {
            renderShadowComponent(*shadowComp);
        }
        // Check for cascaded shadow component
        if (auto csmComp = entity->tryGetComponent<Rapture::CascadedShadowComponent>()) {
            renderCascadedShadowComponent(*csmComp);
        }

        if (auto meshComp = entity->tryGetComponent<Rapture::MeshComponent>()) {
            renderMeshComponent(*meshComp);
        }

        if (auto fogComp = entity->tryGetComponent<Rapture::FogComponent>()) {
            renderFogComponent(*fogComp);
        }

        if (auto ilComp = entity->tryGetComponent<Rapture::IndirectLightingComponent>()) {
            renderIndirectLightingComponent(*ilComp);
        }

        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("AddComponentMenu");
        }

        if (ImGui::BeginPopup("AddComponentMenu")) {
            renderAddComponentMenu(*entity);
            ImGui::EndPopup();
        }

        if (auto skyboxComp = entity->tryGetComponent<Rapture::SkyboxComponent>(); skyboxComp != nullptr) {
            renderSkyboxComponent(*skyboxComp);
        }

        if (auto terrainComp = entity->tryGetComponent<Rapture::TerrainComponent>(); terrainComp != nullptr) {
            renderTerrainComponent(*terrainComp);
        }
    }

    BetterUi::EndContent();
    BetterUi::EndPanel();
}

void PropertiesPanel::renderMaterialComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Material Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &material = entity->getComponent<Rapture::MaterialComponent>();

            ImGui::BeginTable("materialTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Base Material");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", material.material->getBaseMaterial()->getName().c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Material Instance");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", material.material->getName().c_str());

            auto baseMat = material.material->getBaseMaterial();
            for (Rapture::ParameterID paramID : baseMat->getEditableParams()) {
                const Rapture::ParamInfo *info = Rapture::getParamInfo(paramID);
                if (!info || info->type == Rapture::ParamType::TEXTURE) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", std::string(info->name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);

                std::string label = "##" + std::string(info->name);

                if (info->type == Rapture::ParamType::VEC4 || info->type == Rapture::ParamType::VEC3) {
                    glm::vec4 val = material.material->getParameter<glm::vec4>(paramID);
                    glm::vec3 color(val.x, val.y, val.z);
                    if (ImGui::ColorEdit3(label.c_str(), glm::value_ptr(color))) {
                        material.material->setParameter(paramID, glm::vec4(color, val.w));
                    }
                } else if (info->type == Rapture::ParamType::FLOAT) {
                    float val = material.material->getParameter<float>(paramID);
                    if (ImGui::DragFloat(label.c_str(), &val, 0.01f, 0.0f, 1.0f)) {
                        material.material->setParameter(paramID, val);
                    }
                }
            }

            ImGui::EndTable();
        }
    }
}

void PropertiesPanel::renderLightComponent(Rapture::LightComponent &lightComp)
{
    auto &compReg = ComponentUI::ComponentLayoutRegistry::getInstance();
    ComponentUI::renderComponentLayout<Rapture::LightComponent>(compReg.getLightLayout(), lightComp, componentTmpStorage);
}

void transformComponentSlider(glm::vec3 &value, float sliderWidth, bool &changed, const std::string label[3])
{

    // X axis (Red)
    // ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "X:");
    // ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.1f, 0.1f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    ImGui::PushItemWidth(sliderWidth);
    if (ImGui::DragFloat(label[0].c_str(), &value.x, 0.1f)) changed = true;
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    // Y axis (Green)
    // ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Y:");

    // ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.6f, 0.1f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
    ImGui::PushItemWidth(sliderWidth);
    if (ImGui::DragFloat(label[1].c_str(), &value.y, 0.1f)) changed = true;
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    // Z axis (Blue)
    // ImGui::TextColored(ImVec4(0.2f, 0.2f, 1.0f, 1.0f), "Z:");
    // ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.6f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 0.2f, 1.0f, 1.0f));
    ImGui::PushItemWidth(sliderWidth);
    if (ImGui::DragFloat(label[2].c_str(), &value.z, 0.1f)) changed = true;
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
}

void PropertiesPanel::renderTransformComponent()
{
    if (auto entity = m_selectedEntity.lock()) {

        if (ImGui::CollapsingHeader("Transform Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &transform = entity->getComponent<Rapture::TransformComponent>();
            ImGui::Dummy(ImVec2(0.0f, 10.0f));

            // Position with lock option
            ImGui::BeginTable("transformTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp);

            // ========== Position Slider ==========
            ImGui::TableNextRow();

            glm::vec3 position = transform.transforms.getTranslation();
            bool positionChanged = false;

            ImGui::TableSetColumnIndex(0);

            ImGui::Text("Position");

            ImGui::TableSetColumnIndex(1);

            float availableWidth = ImGui::GetContentRegionAvail().x;
            float availableHeight = ImGui::GetContentRegionAvail().y;

            float sliderWidth = availableWidth / 3.0f;

            std::string positionLabel[3] = {"##posX", "##posY", "##posZ"};
            transformComponentSlider(position, sliderWidth, positionChanged, positionLabel);

            // If position changed, update the transform
            if (positionChanged) {

                transform.transforms.setTranslation(position);
                transform.transforms.recalculateTransform();
            }

            ImGui::TableSetColumnIndex(2);

            // ========== Rotation Slider ==========

            ImGui::TableNextRow();

            glm::vec3 rotation = transform.transforms.getRotation();
            bool rotationChanged = false;

            ImGui::TableSetColumnIndex(0);

            ImGui::Text("Rotation");

            ImGui::TableSetColumnIndex(1);

            availableWidth = ImGui::GetContentRegionAvail().x;
            availableHeight = ImGui::GetContentRegionAvail().y;

            sliderWidth = availableWidth / 3.0f;

            std::string rotationLabel[3] = {"##rotX", "##rotY", "##rotZ"};
            transformComponentSlider(rotation, sliderWidth, rotationChanged, rotationLabel);

            // If position changed, update the transform
            if (rotationChanged) {

                transform.transforms.setRotation(rotation);
                transform.transforms.recalculateTransform();
            }

            ImGui::TableSetColumnIndex(2);

            // ========== Scale Slider ==========

            ImGui::TableNextRow();

            glm::vec3 scale = transform.transforms.getScale();
            bool scaleChanged = false;

            ImGui::TableSetColumnIndex(0);

            ImGui::Text("Scale");

            ImGui::TableSetColumnIndex(1);

            availableWidth = ImGui::GetContentRegionAvail().x;
            availableHeight = ImGui::GetContentRegionAvail().y;

            sliderWidth = availableWidth / 3.0f;

            std::string scaleLabel[3] = {"##scaleX", "##scaleY", "##scaleZ"};
            transformComponentSlider(scale, sliderWidth, scaleChanged, scaleLabel);

            // If position changed, update the transform
            if (scaleChanged) {

                transform.transforms.setScale(scale);
                transform.transforms.recalculateTransform();
            }

            ImGui::TableSetColumnIndex(2);

            ImGui::EndTable();

            ImGui::Dummy(ImVec2(0.0f, 20.0f));
        }
    }
}

void PropertiesPanel::renderCameraComponent(Rapture::CameraComponent &cameraComp)
{

    auto &compReg = ComponentUI::ComponentLayoutRegistry::getInstance();

    bool anyChanged =
        ComponentUI::renderComponentLayout<Rapture::CameraComponent>(compReg.getCameraLayout(), cameraComp, componentTmpStorage);

    if (anyChanged) {
        cameraComp.updateProjectionMatrix(cameraComp.fov, cameraComp.aspectRatio, cameraComp.nearPlane, cameraComp.farPlane);
    }
}

void PropertiesPanel::renderShadowComponent(Rapture::ShadowComponent &shadowComp)
{
    (void)shadowComp;
}

void PropertiesPanel::renderCascadedShadowComponent(Rapture::CascadedShadowComponent &csmComp)
{

    auto &compReg = ComponentUI::ComponentLayoutRegistry::getInstance();
    ComponentUI::renderComponentLayout<Rapture::CascadedShadowComponent>(compReg.getCascadedShadowLayout(), csmComp,
                                                                         componentTmpStorage);
}

void PropertiesPanel::renderMeshComponent(Rapture::MeshComponent &meshComp)
{

    auto &compReg = ComponentUI::ComponentLayoutRegistry::getInstance();

    ComponentUI::renderComponentLayout<Rapture::MeshComponent>(compReg.getMeshLayout(), meshComp, componentTmpStorage);
}

void PropertiesPanel::renderFogComponent(Rapture::FogComponent &fogComp)
{
    auto &compReg = ComponentUI::ComponentLayoutRegistry::getInstance();

    ComponentUI::renderComponentLayout<Rapture::FogComponent>(compReg.getFogLayout(), fogComp, componentTmpStorage);
}

void PropertiesPanel::renderIndirectLightingComponent(Rapture::IndirectLightingComponent &ilComp)
{
    ImGui::Separator();
    ImGui::Text("Indirect Lighting Component");

    ImGui::Checkbox("Enabled", &ilComp.enabled);
    ImGui::DragFloat("GI Intensity", &ilComp.giIntensity, 0.01f, 0.0f, 10.0f, "%.2f");

    ImGui::Text("Technique:");
    if (ilComp.isAmbient()) {
        ImGui::Text("  Current: Ambient");
        auto *ambient = ilComp.getAmbientSettings();
        if (ambient) {
            ImGui::ColorEdit3("Ambient Color", &ambient->ambientColor.x);
        }
    } else if (ilComp.isDDGI()) {
        ImGui::Text("  Current: DDGI");
        auto *ddgi = ilComp.getDDGISettings();
        if (ddgi) {
            ImGui::DragInt3("Probe Count", reinterpret_cast<int *>(&ddgi->probeCount.x), 1.0f, 1, 32);
            ImGui::DragFloat3("Probe Spacing", &ddgi->probeSpacing.x, 0.1f, 0.1f, 10.0f);
            ImGui::DragFloat3("Grid Origin", &ddgi->gridOrigin.x, 0.1f);
            ImGui::DragInt("Rays Per Probe", reinterpret_cast<int *>(&ddgi->raysPerProbe), 1.0f, 32, 1024);
            ImGui::DragFloat("Intensity", &ddgi->intensity, 0.01f, 0.0f, 10.0f);
            ImGui::Checkbox("Visualize Probes", &ddgi->visualizeProbes);
        }
    } else {
        ImGui::Text("  Current: Disabled");
    }
}

void PropertiesPanel::renderSkyboxComponent(Rapture::SkyboxComponent &skyboxComp)
{
    auto &compReg = ComponentUI::ComponentLayoutRegistry::getInstance();

    ComponentUI::renderComponentLayout<Rapture::SkyboxComponent>(compReg.getSkyboxLayout(), skyboxComp, componentTmpStorage);
}

void PropertiesPanel::renderAddComponentMenu(Rapture::Entity entity)
{
    if (!entity.isValid()) {
        return;
    }

    ImGui::Text("Add Component");
    ImGui::Separator();

    // Helper lambda to safely add a component
    auto tryAddComponent = [&entity](auto &&addFunc, const char *name) {
        try {
            addFunc();
            return true;
        } catch (const Rapture::EntityException &e) {
            // Component already exists, skip it
            (void)e;
            return false;
        } catch (const std::exception &e) {
            Rapture::RP_ERROR("Failed to add component {}: {}", name, e.what());
            return false;
        }
    };

    // Material Component

    // Mesh Component
    if (!entity.hasComponent<Rapture::MeshComponent>()) {
        if (ImGui::MenuItem("Mesh Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::MeshComponent>(); }, "Mesh Component");
        }
    }

    // BLAS Component
    if (!entity.hasComponent<Rapture::BLASComponent>()) {
        if (auto meshComp = entity.tryGetComponent<Rapture::MeshComponent>(); meshComp != nullptr) {
            if (ImGui::MenuItem("BLAS Component")) {
                tryAddComponent([&entity, meshComp]() { entity.addComponent<Rapture::BLASComponent>(meshComp->mesh); },
                                "BLAS Component");
                entity.getScene()->registerBLAS(entity);
            }
        }
    }

    // Light Component
    if (!entity.hasComponent<Rapture::LightComponent>()) {
        if (ImGui::MenuItem("Light Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::LightComponent>(); }, "Light Component");
        }
    }

    // Camera Component
    if (!entity.hasComponent<Rapture::CameraComponent>()) {
        if (ImGui::MenuItem("Camera Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::CameraComponent>(); }, "Camera Component");
        }
    }

    // Camera Controller Component
    if (!entity.hasComponent<Rapture::CameraControllerComponent>()) {
        if (ImGui::MenuItem("Camera Controller Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::CameraControllerComponent>(); },
                            "Camera Controller Component");
        }
    }

    // Fog Component
    if (!entity.hasComponent<Rapture::FogComponent>()) {
        if (ImGui::MenuItem("Fog Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::FogComponent>(); }, "Fog Component");
        }
    }

    // Indirect Lighting Component
    if (!entity.hasComponent<Rapture::IndirectLightingComponent>()) {
        if (ImGui::MenuItem("Indirect Lighting Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::IndirectLightingComponent>(); },
                            "Indirect Lighting Component");
        }
    }

    // Bounding Box Component
    if (!entity.hasComponent<Rapture::BoundingBoxComponent>()) {
        if (ImGui::MenuItem("Bounding Box Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::BoundingBoxComponent>(); }, "Bounding Box Component");
        }
    }

    // Skybox Component
    if (!entity.hasComponent<Rapture::SkyboxComponent>()) {
        if (ImGui::MenuItem("Skybox Component")) {
            tryAddComponent([&entity]() { entity.addComponent<Rapture::SkyboxComponent>(); }, "Skybox Component");
        }
    }
}

void PropertiesPanel::renderTerrainComponent(Rapture::TerrainComponent &terrainComp)
{
    if (!terrainComp.generator) {
        return;
    }

    if (ImGui::CollapsingHeader("Terrain Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &terrainComp.isEnabled);

        if (!terrainComp.generator->isInitialized()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Terrain not initialized");
            return;
        }

        auto &config = terrainComp.generator->getConfigMutable();

        float heightScale = config.heightScale;
        if (ImGui::DragFloat("Height Scale", &heightScale, 1.0f, 0.0f, 1000.0f)) {
            terrainComp.generator->setHeightScale(heightScale);
        }

        ImGui::DragFloat("Chunk Size", &config.chunkWorldSize, 1.0f, 1.0f, 256.0f);
        ImGui::DragFloat("Terrain Size", &config.terrainWorldSize, 10.0f, 64.0f, 8192.0f);

        ImGui::Separator();

        const char *modeNames[] = {"Single Heightmap", "Multi-Noise (CEPV)"};
        int currentMode = static_cast<int>(config.hmType);
        if (ImGui::Combo("Heightmap Mode", &currentMode, modeNames, IM_ARRAYSIZE(modeNames))) {
            config.hmType = static_cast<Rapture::HeightmapType>(currentMode);
            m_terrainTextureCache.clear();
        }

        ImGui::Separator();
        ImGui::Text("Chunk Grid: %u (radius %d)", terrainComp.generator->getChunkCount(), config.getChunkRadius());

        bool wireframe = terrainComp.generator->isWireframe();
        if (ImGui::Checkbox("Wireframe", &wireframe)) {
            terrainComp.generator->setWireframe(wireframe);
        }

        ImGui::Separator();

        auto renderTextureCombo = [&](const char *label, Rapture::TerrainNoiseCategory category) {
            Rapture::Texture *selectedTexture = terrainComp.generator->getNoiseTexture(category);
            std::string previewName = "None";

            if (selectedTexture && m_terrainTextureCache.cachedHandles[category] == 0) {
                auto allHandles = Rapture::AssetManager::getTextures();
                for (auto handle : allHandles) {
                    auto asset = Rapture::AssetManager::getAsset(handle);
                    if (asset && asset.get()->getUnderlyingAsset<Rapture::Texture>() == selectedTexture) {
                        m_terrainTextureCache.cachedHandles[category] = handle;
                        break;
                    }
                }
            }

            if (m_terrainTextureCache.cachedHandles[category] != 0) {
                auto &metadata = Rapture::AssetManager::getAssetMetadata(m_terrainTextureCache.cachedHandles[category]);
                previewName = metadata.getName();
            }

            ImGui::SetNextWindowSizeConstraints(
                ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * m_terrainTextureCache.MAX_VISIBLE));
            if (ImGui::BeginCombo(label, previewName.c_str())) {
                auto allHandles = Rapture::AssetManager::getTextures();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(allHandles.size()));
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        auto handle = allHandles[i];
                        auto asset = Rapture::AssetManager::getAsset(handle);
                        if (!asset) continue;

                        auto texture = asset.get()->getUnderlyingAsset<Rapture::Texture>();
                        if (!texture) continue;

                        auto &metadata = Rapture::AssetManager::getAssetMetadata(handle);
                        bool isSelected = (m_terrainTextureCache.cachedHandles[category] == handle);

                        if (ImGui::Selectable(metadata.getName().c_str(), isSelected)) {
                            terrainComp.generator->setNoiseTexture(category, texture);
                            m_terrainTextureCache.cachedHandles[category] = handle;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
                ImGui::EndCombo();
            }
        };

        if (config.hmType == Rapture::HeightmapType::HM_SINGLE) {
            ImGui::Text("Single Heightmap");
            renderTextureCombo("Heightmap Texture", Rapture::CONTINENTALNESS);
        }

        ImGui::Separator();

        if (config.hmType == Rapture::HeightmapType::HM_CEPV) {
            ImGui::Text("Multi-Noise Textures");

            auto &multiNoise = terrainComp.generator->getMultiNoiseConfig();
            bool splineChanged = false;

            const char *categoryNames[] = {"Continentalness", "Erosion", "Peaks & Valleys"};

            for (uint8_t i = 0; i < Rapture::TERRAIN_NC_COUNT; ++i) {
                std::string label = std::string(categoryNames[i]) + " Texture";
                renderTextureCombo(label.c_str(), static_cast<Rapture::TerrainNoiseCategory>(i));
            }

            ImGui::Separator();

            if (ImGui::TreeNode("Multi-Noise Splines")) {
                for (uint8_t cat = 0; cat < Rapture::TERRAIN_NC_COUNT; ++cat) {
                    auto &spline = multiNoise.splines[cat];
                    Modules::SplinePoints splinePoints =
                        Modules::createSplinePoints(&spline.points, Modules::InterpolationType::LINEAR);
                    if (Modules::plotEditor(categoryNames[cat], splinePoints, ImVec2(0, 150))) {
                        splineChanged = true;
                    }
                    ImGui::Spacing();
                }
                ImGui::TreePop();
            }

            if (splineChanged || ImGui::Button("Rebake LUT")) {
                terrainComp.generator->bakeNoiseLUT();
            }
        }

        ImGui::Separator();
    }
}
