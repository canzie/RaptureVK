#include "ProprtiesPanel.h"

#include "Events/GameEvents.h"

#include "Components/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

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
}

PropertiesPanel::~PropertiesPanel()
{
    Rapture::GameEvents::onEntitySelected().removeListener(m_entitySelectedListenerId);
}

void PropertiesPanel::render()
{
    ImGui::Begin("Properties");

    if (auto entity = m_selectedEntity.lock()) {
        if (entity->hasComponent<Rapture::TransformComponent>()) {
            renderTransformComponent();
        }
        if (entity->hasComponent<Rapture::MaterialComponent>()) {
            renderMaterialComponent();
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





