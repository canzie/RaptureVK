#include "ProprtiesPanel.h"

#include "Events/GameEvents.h"

#include "Components/Components.h"
#include "Textures/Texture.h"


#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Logging/TracyProfiler.h"

// Implementation of HelpMarker function
void PropertiesPanel::HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
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
        [this](std::shared_ptr<Rapture::Entity> entity) {
            m_selectedEntity = entity;
    });
    
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

    ImGui::Begin("Properties");

    

    if (auto entity = m_selectedEntity.lock()) {
        if (entity->hasComponent<Rapture::TransformComponent>()) {
            renderTransformComponent();
        }
        if (entity->hasComponent<Rapture::MaterialComponent>()) {
            renderMaterialComponent();
        }
        if (entity->hasComponent<Rapture::LightComponent>()) {
            renderLightComponent();
        }
        if (entity->hasComponent<Rapture::CameraComponent>()) {
            renderCameraComponent();
        }
        // Check for shadow component only if entity has both transform and light components
        if (entity->hasAllComponents<Rapture::TransformComponent, Rapture::LightComponent, Rapture::ShadowComponent>()) {
            renderShadowComponent();
        }
        // Check for cascaded shadow component
        if (entity->hasAllComponents<Rapture::TransformComponent, Rapture::LightComponent, Rapture::CascadedShadowComponent>()) {
            renderCascadedShadowComponent();
        }

        if (entity->hasComponent<Rapture::MeshComponent>()) {
            renderMeshComponent();
        }
    }

    ImGui::End();
}

void PropertiesPanel::renderMaterialComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Material Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& material = entity->getComponent<Rapture::MaterialComponent>();

            ImGui::Text("Base Material: %s", material.material->getBaseMaterial()->getName().c_str());
            ImGui::Text("Material Instance: %s", material.material->getName().c_str());

            auto& parameterMap = material.material->getParameterMap();

            for (auto& [paramID, materialParameter] : parameterMap) {
                ImGui::Text("%s", Rapture::parameterIdToString(paramID).c_str());
                if (paramID == Rapture::ParameterID::ALBEDO) {
                    ImGui::SameLine();
                    glm::vec3 albedo = materialParameter.asVec3();
                    if (ImGui::ColorEdit3("##albedo", glm::value_ptr(albedo))) {
                        material.material->setParameter(Rapture::ParameterID::ALBEDO, albedo);
                    }
                }

                if (paramID == Rapture::ParameterID::ROUGHNESS) {
                    ImGui::SameLine();
                    float roughness = materialParameter.asFloat();
                    if (ImGui::DragFloat("##roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
                        material.material->setParameter(Rapture::ParameterID::ROUGHNESS, roughness);
                    }
                }

                if (paramID == Rapture::ParameterID::METALLIC) {
                    ImGui::SameLine();
                    float metallic = materialParameter.asFloat();
                    if (ImGui::DragFloat("##metallic", &metallic, 0.01f, 0.0f, 1.0f)) {
                        material.material->setParameter(Rapture::ParameterID::METALLIC, metallic);
                    }
                }
            }

        }
    }
}

void PropertiesPanel::renderLightComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = entity->getComponent<Rapture::LightComponent>();

            // Light Type
            const char* lightTypeNames[] = { "Point", "Directional", "Spot" };
            int currentType = static_cast<int>(light.type);
            if (ImGui::Combo("Type", &currentType, lightTypeNames, IM_ARRAYSIZE(lightTypeNames))) {
                light.type = static_cast<Rapture::LightType>(currentType);
            }

            // Color
            if (ImGui::ColorEdit3("Color", glm::value_ptr(light.color))) {
                // Value already updated by ImGui
            }

            // Intensity
            ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 100.0f);

            // Range (for Point and Spot lights)
            if (light.type == Rapture::LightType::Point || light.type == Rapture::LightType::Spot) {
                ImGui::DragFloat("Range", &light.range, 0.1f, 0.0f, 1000.0f);
            }

            // Spot Light Angles (for Spot lights only)
            if (light.type == Rapture::LightType::Spot) {
                float innerAngleDegrees = glm::degrees(light.innerConeAngle);
                float outerAngleDegrees = glm::degrees(light.outerConeAngle);
                if (ImGui::DragFloat("Inner Cone Angle", &innerAngleDegrees, 0.1f, 0.0f, outerAngleDegrees)) {
                    light.innerConeAngle = glm::radians(innerAngleDegrees);
                }
                if (ImGui::DragFloat("Outer Cone Angle", &outerAngleDegrees, 0.1f, innerAngleDegrees, 89.0f)) {
                    light.outerConeAngle = glm::radians(outerAngleDegrees);
                }
            }

            // Is Active
            ImGui::Checkbox("Is Active", &light.isActive);

            // Casts Shadow
            ImGui::Checkbox("Casts Shadow", &light.castsShadow);
        }
    }
}

void transformComponentSlider(glm::vec3& value, float sliderWidth, bool& changed, const std::string label[3]) {

          // X axis (Red)
            //ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "X:");
            //ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.1f, 0.1f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::PushItemWidth(sliderWidth);
            if (ImGui::DragFloat(label[0].c_str(), &value.x, 0.1f)) changed = true;
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            // Y axis (Green)
            //ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Y:");

            //ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.6f, 0.1f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
            ImGui::PushItemWidth(sliderWidth);
            if (ImGui::DragFloat(label[1].c_str(), &value.y, 0.1f)) changed = true;
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            // Z axis (Blue)
            //ImGui::TextColored(ImVec4(0.2f, 0.2f, 1.0f, 1.0f), "Z:");
            //ImGui::SameLine();
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
        auto& transform = entity->getComponent<Rapture::TransformComponent>();
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

void PropertiesPanel::renderCameraComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& cameraComponent = entity->getComponent<Rapture::CameraComponent>();

            bool cameraChanged = false;

            // FOV
            if (ImGui::DragFloat("FOV", &cameraComponent.fov, 0.1f, 1.0f, 179.0f)) {
                cameraChanged = true;
            }

            // Aspect Ratio
            if (ImGui::DragFloat("Aspect Ratio", &cameraComponent.aspectRatio, 0.01f, 0.1f, 10.0f)) {
                cameraChanged = true;
            }

            // Near Plane
            if (ImGui::DragFloat("Near Plane", &cameraComponent.nearPlane, 0.01f, 0.01f, cameraComponent.farPlane - 0.01f)) {
                cameraChanged = true;
            }

            // Far Plane
            if (ImGui::DragFloat("Far Plane", &cameraComponent.farPlane, 0.1f, cameraComponent.nearPlane + 0.01f, 10000.0f)) {
                cameraChanged = true;
            }


            if (cameraChanged) {
                cameraComponent.updateProjectionMatrix(cameraComponent.fov, cameraComponent.aspectRatio, cameraComponent.nearPlane, cameraComponent.farPlane);
            }
        }
    }
}

void PropertiesPanel::renderShadowComponent() {

    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Shadow Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& shadow = entity->getComponent<Rapture::ShadowComponent>();
            auto& light = entity->getComponent<Rapture::LightComponent>();

            // Only show shadow map if the light casts shadows
            if (shadow.shadowMap) {
                auto shadowTexture = shadow.shadowMap->getShadowTexture();
                if (shadowTexture) {
                    // Get the texture dimensions
                    const auto& spec = shadowTexture->getSpecification();
                    float aspectRatio = static_cast<float>(spec.width) / static_cast<float>(spec.height);
                    
                    // Calculate display size while maintaining aspect ratio
                    float displayWidth = ImGui::GetContentRegionAvail().x;
                    float displayHeight = displayWidth / aspectRatio;
                    
                    // Create ImGui descriptor for the shadow map texture
                    VkDescriptorImageInfo imageInfo = shadowTexture->getDescriptorImageInfo(Rapture::TextureViewType::DEPTH);
                    if (m_currentShadowMapDescriptorSet != VK_NULL_HANDLE) {
                        ImGui_ImplVulkan_RemoveTexture(m_currentShadowMapDescriptorSet);
                    }
                    m_currentShadowMapDescriptorSet = ImGui_ImplVulkan_AddTexture(
                        imageInfo.sampler,
                        imageInfo.imageView,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    );
                    
                    // Display the shadow map texture
                    ImGui::Text("Shadow Map (%dx%d)", spec.width, spec.height);
                    ImGui::Image(
                        (ImTextureID)m_currentShadowMapDescriptorSet,
                        ImVec2(displayWidth, displayHeight)
                    );


                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Shadow map not available");
                ImGui::Text("Enable 'Casts Shadow' in the Light Component to generate shadow maps");
            }
        }
    }

}

void PropertiesPanel::renderCascadedShadowComponent() {
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Cascaded Shadow Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& csmShadow = entity->getComponent<Rapture::CascadedShadowComponent>();
            auto& light = entity->getComponent<Rapture::LightComponent>();

            if (csmShadow.cascadedShadowMap) {
                // Lambda parameter for cascade distribution
                float currentLambda = csmShadow.cascadedShadowMap->getLambda();
                if (ImGui::SliderFloat("Lambda", &currentLambda, 0.0f, 1.0f, "%.3f")) {
                    csmShadow.cascadedShadowMap->setLambda(currentLambda);
                }
                ImGui::SameLine();
                HelpMarker("Controls cascade split distribution: 0.0 = linear splits, 1.0 = logarithmic splits");
                
                // Display cascade information
                ImGui::Separator();
                ImGui::Text("Cascade Information:");
                
                uint8_t numCascades = csmShadow.cascadedShadowMap->getNumCascades();
                ImGui::Text("Number of Cascades: %d", numCascades);
                
                // Display shadow map texture information
                ImGui::Separator();
                auto shadowTexture = csmShadow.cascadedShadowMap->getShadowTexture();
                auto flattenedShadowTexture = csmShadow.cascadedShadowMap->getFlattenedShadowTexture();
                
                if (shadowTexture) {
                    const auto& spec = shadowTexture->getSpecification();
                    ImGui::Text("Shadow Map Array:");
                    ImGui::Text("  Resolution: %dx%d", spec.width, spec.height);
                    ImGui::Text("  Layers: %d", spec.depth);
                    ImGui::Text("  Format: %s", spec.format == Rapture::TextureFormat::D32F ? "D32F" : "Unknown");
                    ImGui::Text("  Bindless Texture Handle: %d", csmShadow.cascadedShadowMap->getTextureHandle());
                    for (size_t i = 0; i < csmShadow.cascadedShadowMap->getCascadeSplits().size() - 1; i++) {
                        ImGui::Text("  Cascade %zu:", i);
                        ImGui::Text("    Near: %.3f", csmShadow.cascadedShadowMap->getCascadeSplits()[i]);
                        ImGui::Text("    Far: %.3f", csmShadow.cascadedShadowMap->getCascadeSplits()[i + 1]);
                    }
                    
                    // Display flattened shadow map texture if available
                    if (flattenedShadowTexture && flattenedShadowTexture->isReadyForSampling()) {
                        ImGui::Separator();
                        ImGui::Text("Flattened Shadow Map Visualization:");
                        
                        const auto& flatSpec = flattenedShadowTexture->getSpecification();
                        float aspectRatio = static_cast<float>(flatSpec.width) / static_cast<float>(flatSpec.height);
                        
                        // Calculate display size while maintaining aspect ratio
                        float displayWidth = ImGui::GetContentRegionAvail().x;
                        float displayHeight = displayWidth / aspectRatio;
                        
                        // Limit the height to prevent overly tall images
                        const float maxHeight = 400.0f;
                        if (displayHeight > maxHeight) {
                            displayHeight = maxHeight;
                            displayWidth = displayHeight * aspectRatio;
                        }
                        
                        // Create ImGui descriptor for the flattened shadow map texture
                        VkDescriptorImageInfo imageInfo = flattenedShadowTexture->getDescriptorImageInfo(Rapture::TextureViewType::DEFAULT);
                        if (m_currentCSMDescriptorSet != VK_NULL_HANDLE) {
                            ImGui_ImplVulkan_RemoveTexture(m_currentCSMDescriptorSet);
                        }
                        m_currentCSMDescriptorSet = ImGui_ImplVulkan_AddTexture(
                            imageInfo.sampler,
                            imageInfo.imageView,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        );
                        
                        // Display the flattened shadow map texture
                        ImGui::Text("Flattened Resolution: %dx%d", flatSpec.width, flatSpec.height);
                        ImGui::Image(
                            (ImTextureID)m_currentCSMDescriptorSet,
                            ImVec2(displayWidth, displayHeight)
                        );
                        
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Each square represents one cascade layer");
                    } else {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "Flattened shadow map not ready");
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Shadow map array not available");
                }
                
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cascaded shadow map not available");
                ImGui::Text("Enable 'Casts Shadow' in the Light Component to generate shadow maps");
            }
        }
    }
}

void PropertiesPanel::renderMeshComponent() {

    bool isInstanced = false;

    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& meshComponent = entity->getComponent<Rapture::MeshComponent>();

            if (entity->hasComponent<Rapture::InstanceComponent>()) {
                isInstanced = true;
            }
            ImGui::Checkbox("Instanced", &isInstanced);

        }
    }


}
