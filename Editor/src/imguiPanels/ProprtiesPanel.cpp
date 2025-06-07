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
}

PropertiesPanel::~PropertiesPanel()
{
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);

    if (m_currentShadowMapDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_currentShadowMapDescriptorSet);
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

void PropertiesPanel::renderTransformComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
    if (ImGui::CollapsingHeader("Transform Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& transform = entity->getComponent<Rapture::TransformComponent>();
        
        // Position with lock option
        ImGui::BeginGroup();
        
 
        // Get translation values to modify them individually
        glm::vec3 position = transform.transforms.getTranslation();
        bool positionChanged = false;
        
        // X axis (Red)
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "X:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.1f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3.0f - 10.0f);
        if (ImGui::DragFloat("##posX", &position.x, 0.1f)) positionChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // Y axis (Green)
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Y:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.6f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2.0f - 10.0f);
        if (ImGui::DragFloat("##posY", &position.y, 0.1f)) positionChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // Z axis (Blue)
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 1.0f, 1.0f), "Z:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.6f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 0.2f, 1.0f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
        if (ImGui::DragFloat("##posZ", &position.z, 0.1f)) positionChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // If position changed, update the transform
        if (positionChanged) {

            transform.transforms.setTranslation(position);
            transform.transforms.recalculateTransform();
        }
        
        ImGui::EndGroup();
        
        // Rotation with lock option
        ImGui::BeginGroup();
        
        // Get rotation values
        glm::vec3 rotation = transform.transforms.getRotation();
        bool rotationChanged = false;
        
        // X rotation (Red)
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "X:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.1f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3.0f - 10.0f);
        if (ImGui::DragFloat("##rotX", &rotation.x, 0.1f)) rotationChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // Y rotation (Green)
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Y:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.6f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2.0f - 10.0f);
        if (ImGui::DragFloat("##rotY", &rotation.y, 0.1f)) rotationChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // Z rotation (Blue)
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 1.0f, 1.0f), "Z:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.6f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 0.2f, 1.0f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
        if (ImGui::DragFloat("##rotZ", &rotation.z, 0.1f)) rotationChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // If rotation changed, update the transform
        if (rotationChanged) {
            transform.transforms.setRotation(rotation);
            transform.transforms.recalculateTransform();
        }
        
        ImGui::EndGroup();
        
        // Scale with lock option (maintain aspect ratio)
        ImGui::BeginGroup();
        
        // Get scale values
        glm::vec3 scale = transform.transforms.getScale();
        // Store original scale
        glm::vec3 originalScale = scale;
        bool scaleChanged = false;
        
        // X scale (Red)
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "X:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.6f, 0.1f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 3.0f - 10.0f);
        if (ImGui::DragFloat("##scaleX", &scale.x, 0.1f)) scaleChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // Y scale (Green)
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Y:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.6f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / 2.0f - 10.0f);
        if (ImGui::DragFloat("##scaleY", &scale.y, 0.1f)) scaleChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // Z scale (Blue)
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 1.0f, 1.0f), "Z:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.6f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.2f, 0.2f, 1.0f, 1.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
        if (ImGui::DragFloat("##scaleZ", &scale.z, 0.1f)) scaleChanged = true;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        
        // If scale changed, update the transform
        if (scaleChanged) {
            
            transform.transforms.setScale(scale);
            transform.transforms.recalculateTransform();

        }
        ImGui::EndGroup();
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





