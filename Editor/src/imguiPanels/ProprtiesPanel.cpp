#include "ProprtiesPanel.h"

#include "Events/GameEvents.h"

#include "AssetManager/AssetManager.h"

#include "Scenes/Entities/Entity.h"
#include "Textures/Texture.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Logging/Log.h"
#include "Logging/TracyProfiler.h"

#include "imguiPanels/modules/PlotEditor.h"

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

            if (entity->hasComponent<Rapture::Entropy::RigidBodyComponent>()) {
                renderRigidBodyComponent();
            }
        }

        if (entity->hasComponent<Rapture::FogComponent>()) {
            renderFogComponent();
        }

        if (entity->hasComponent<Rapture::IndirectLightingComponent>()) {
            renderIndirectLightingComponent();
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

    ImGui::End();
}

void PropertiesPanel::renderMaterialComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Material Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &material = entity->getComponent<Rapture::MaterialComponent>();

            ImGui::Text("Base Material: %s", material.material->getBaseMaterial()->getName().c_str());
            ImGui::Text("Material Instance: %s", material.material->getName().c_str());

            auto baseMat = material.material->getBaseMaterial();
            for (Rapture::ParameterID paramID : baseMat->getEditableParams()) {
                const Rapture::ParamInfo *info = Rapture::getParamInfo(paramID);
                if (!info || info->type == Rapture::ParamType::TEXTURE) continue;

                ImGui::Text("%s", std::string(info->name).c_str());
                ImGui::SameLine();

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
        }
    }
}

void PropertiesPanel::renderLightComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &light = entity->getComponent<Rapture::LightComponent>();

            // Light Type
            const char *lightTypeNames[] = {"Point", "Directional", "Spot"};
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

void PropertiesPanel::renderCameraComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &cameraComponent = entity->getComponent<Rapture::CameraComponent>();

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
                cameraComponent.updateProjectionMatrix(cameraComponent.fov, cameraComponent.aspectRatio, cameraComponent.nearPlane,
                                                       cameraComponent.farPlane);
            }
        }
    }
}

void PropertiesPanel::renderShadowComponent()
{

    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Shadow Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &shadow = entity->getComponent<Rapture::ShadowComponent>();
            auto &light = entity->getComponent<Rapture::LightComponent>();

            // Only show shadow map if the light casts shadows
            if (shadow.shadowMap) {
                auto shadowTexture = shadow.shadowMap->getShadowTexture();
                if (shadowTexture) {
                    // Get the texture dimensions
                    const auto &spec = shadowTexture->getSpecification();
                    float aspectRatio = static_cast<float>(spec.width) / static_cast<float>(spec.height);

                    // Calculate display size while maintaining aspect ratio
                    float displayWidth = ImGui::GetContentRegionAvail().x;
                    float displayHeight = displayWidth / aspectRatio;

                    // Create ImGui descriptor for the shadow map texture
                    VkDescriptorImageInfo imageInfo = shadowTexture->getDescriptorImageInfo(Rapture::TextureViewType::DEPTH);
                    if (m_currentShadowMapDescriptorSet != VK_NULL_HANDLE) {
                        ImGui_ImplVulkan_RemoveTexture(m_currentShadowMapDescriptorSet);
                    }
                    m_currentShadowMapDescriptorSet = ImGui_ImplVulkan_AddTexture(imageInfo.sampler, imageInfo.imageView,
                                                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                    // Display the shadow map texture
                    ImGui::Text("Shadow Map (%dx%d)", spec.width, spec.height);
                    ImGui::Image((ImTextureID)m_currentShadowMapDescriptorSet, ImVec2(displayWidth, displayHeight));
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Shadow map not available");
                ImGui::Text("Enable 'Casts Shadow' in the Light Component to generate shadow maps");
            }
        }
    }
}

void PropertiesPanel::renderCascadedShadowComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Cascaded Shadow Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &csmShadow = entity->getComponent<Rapture::CascadedShadowComponent>();
            auto &light = entity->getComponent<Rapture::LightComponent>();

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
                    const auto &spec = shadowTexture->getSpecification();
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

                        const auto &flatSpec = flattenedShadowTexture->getSpecification();
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
                        VkDescriptorImageInfo imageInfo =
                            flattenedShadowTexture->getDescriptorImageInfo(Rapture::TextureViewType::DEFAULT);
                        if (m_currentCSMDescriptorSet != VK_NULL_HANDLE) {
                            ImGui_ImplVulkan_RemoveTexture(m_currentCSMDescriptorSet);
                        }
                        m_currentCSMDescriptorSet = ImGui_ImplVulkan_AddTexture(imageInfo.sampler, imageInfo.imageView,
                                                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                        // Display the flattened shadow map texture
                        ImGui::Text("Flattened Resolution: %dx%d", flatSpec.width, flatSpec.height);
                        ImGui::Image((ImTextureID)m_currentCSMDescriptorSet, ImVec2(displayWidth, displayHeight));

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

void PropertiesPanel::renderMeshComponent()
{

    bool isInstanced = false;

    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &meshComponent = entity->getComponent<Rapture::MeshComponent>();

            if (entity->hasComponent<Rapture::InstanceComponent>()) {
                isInstanced = true;
            }
            ImGui::Checkbox("Instanced", &isInstanced);
        }
    }
}

void PropertiesPanel::renderRigidBodyComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        if (ImGui::CollapsingHeader("Rigid Body Component", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &rigidbody = entity->getComponent<Rapture::Entropy::RigidBodyComponent>();
            auto *collider = rigidbody.collider.get();

            if (!collider) {
                ImGui::Text("No collider attached.");
                return;
            }

            // Mass
            auto colliderType = collider->getColliderType();
            if (colliderType == Rapture::Entropy::ColliderType::Sphere || colliderType == Rapture::Entropy::ColliderType::AABB ||
                colliderType == Rapture::Entropy::ColliderType::OBB) {
                float mass = (rigidbody.invMass > 0.0f) ? 1.0f / rigidbody.invMass : 0.0f;
                if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.0f, 1000.0f)) {
                    if (mass > 0.0001f) {
                        rigidbody.setMass(mass);
                    } else {
                        rigidbody.invMass = 0.0f;
                        rigidbody.invInertiaTensor = glm::mat3(0.0f);
                    }
                }
            } else {
                ImGui::Text("Mass editing not yet supported for this collider type.");
            }

            ImGui::Separator();
            ImGui::Text("State Vectors");
            ImGui::InputFloat3("Velocity", &rigidbody.velocity[0], "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat3("Angular Velocity", &rigidbody.angularVelocity[0], "%.3f", ImGuiInputTextFlags_ReadOnly);

            glm::vec4 orientation(rigidbody.orientation.x, rigidbody.orientation.y, rigidbody.orientation.z,
                                  rigidbody.orientation.w);
            ImGui::InputFloat4("Orientation (xyzw)", &orientation[0], "%.3f", ImGuiInputTextFlags_ReadOnly);

            ImGui::Separator();
            ImGui::Text("Accumulators");
            ImGui::InputFloat3("Accumulated Force", &rigidbody.accumulatedForce[0], "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat3("Accumulated Torque", &rigidbody.accumulatedTorque[0], "%.3f", ImGuiInputTextFlags_ReadOnly);

            ImGui::Separator();
            ImGui::Text("Inverse Inertia Tensor");
            // GLM stores matrices in column-major order, so we need to transpose for row-major display
            glm::mat3 invTensor = glm::transpose(rigidbody.invInertiaTensor);
            ImGui::InputFloat3("##row1", &invTensor[0][0], "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat3("##row2", &invTensor[1][0], "%.3f", ImGuiInputTextFlags_ReadOnly);
            ImGui::InputFloat3("##row3", &invTensor[2][0], "%.3f", ImGuiInputTextFlags_ReadOnly);

            ImGui::Separator();
            ImGui::Text("Collider Properties");

            // Collider specific properties
            switch (collider->getColliderType()) {
            case Rapture::Entropy::ColliderType::Sphere: {
                ImGui::Text("Type: Sphere");
                auto *sphere = static_cast<Rapture::Entropy::SphereCollider *>(collider);
                ImGui::DragFloat3("Center", &sphere->center.x, 0.1f);
                ImGui::DragFloat("Radius", &sphere->radius, 0.1f, 0.0f);
                break;
            }
            case Rapture::Entropy::ColliderType::AABB: {
                ImGui::Text("Type: AABB");
                auto *aabb = static_cast<Rapture::Entropy::AABBCollider *>(collider);
                ImGui::DragFloat3("Min", &aabb->min.x, 0.1f);
                ImGui::DragFloat3("Max", &aabb->max.x, 0.1f);
                break;
            }
            case Rapture::Entropy::ColliderType::OBB: {
                ImGui::Text("Type: OBB");
                auto *obb = static_cast<Rapture::Entropy::OBBCollider *>(collider);
                ImGui::DragFloat3("Center", &obb->center.x, 0.1f);
                ImGui::DragFloat3("Extents", &obb->extents.x, 0.1f);
                glm::vec3 euler = glm::degrees(glm::eulerAngles(obb->orientation));
                if (ImGui::DragFloat3("Orientation", &euler.x, 1.0f)) {
                    obb->orientation = glm::quat(glm::radians(euler));
                }
                break;
            }
            case Rapture::Entropy::ColliderType::Capsule: {
                ImGui::Text("Type: Capsule");
                auto *capsule = static_cast<Rapture::Entropy::CapsuleCollider *>(collider);
                ImGui::DragFloat3("Start", &capsule->start.x, 0.1f);
                ImGui::DragFloat3("End", &capsule->end.x, 0.1f);
                ImGui::DragFloat("Radius", &capsule->radius, 0.1f, 0.0f);
                break;
            }
            case Rapture::Entropy::ColliderType::Cylinder: {
                ImGui::Text("Type: Cylinder");
                auto *cylinder = static_cast<Rapture::Entropy::CylinderCollider *>(collider);
                ImGui::DragFloat3("Start", &cylinder->start.x, 0.1f);
                ImGui::DragFloat3("End", &cylinder->end.x, 0.1f);
                ImGui::DragFloat("Radius", &cylinder->radius, 0.1f, 0.0f);
                break;
            }
            case Rapture::Entropy::ColliderType::ConvexHull: {
                ImGui::Text("Type: Convex Hull");
                auto *convexHull = static_cast<Rapture::Entropy::ConvexHullCollider *>(collider);
                ImGui::Text("Vertices: %zu", convexHull->vertices.size());
                break;
            }
            }
        }
    }
}

void PropertiesPanel::renderFogComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        ImGui::Separator();
        ImGui::Text("Fog Component");

        auto &fogComp = entity->getComponent<Rapture::FogComponent>();

        ImGui::Checkbox("Enabled", &fogComp.enabled);
        ImGui::ColorEdit3("Fog Color", &fogComp.color.x);
        ImGui::DragFloat("Start Distance", &fogComp.start, 0.1f, 0.0f, fogComp.end, "%.2f");
        ImGui::DragFloat("End Distance", &fogComp.end, 0.1f, fogComp.start, 1000.0f, "%.2f");
        ImGui::DragFloat("Density", &fogComp.density, 0.001f, 0.0f, 1.0f, "%.3f");

        const char *fogTypes[] = {"Linear", "Exponential", "ExponentialSquared"};
        int currentType = static_cast<int>(fogComp.type);
        if (ImGui::Combo("Fog Type", &currentType, fogTypes, 3)) {
            fogComp.type = static_cast<Rapture::FogType>(currentType);
        }
    }
}

void PropertiesPanel::renderIndirectLightingComponent()
{
    if (auto entity = m_selectedEntity.lock()) {
        ImGui::Separator();
        ImGui::Text("Indirect Lighting Component");

        auto &ilComp = entity->getComponent<Rapture::IndirectLightingComponent>();

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
}

void PropertiesPanel::renderSkyboxComponent(Rapture::SkyboxComponent &skyboxComp)
{
    ImGui::CollapsingHeader("Skybox Component", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::Checkbox("Enabled", &skyboxComp.isEnabled);
    ImGui::DragFloat("Skybox Intensity", &skyboxComp.skyIntensity, 0.01f, 0.0f, 1.0f);
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
    if (!entity.hasComponent<Rapture::MaterialComponent>()) {
        if (ImGui::MenuItem("Material Component")) {
            tryAddComponent(
                [&entity]() {
                    auto material =
                        Rapture::AssetManager::importDefaultAsset<Rapture::MaterialInstance>(Rapture::AssetType::Material).first;
                    if (material) {
                        entity.addComponent<Rapture::MaterialComponent>(material);
                    }
                },
                "Material Component");
        }
    }

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
    if (ImGui::CollapsingHeader("Terrain Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled", &terrainComp.isEnabled);

        if (!terrainComp.generator.isInitialized()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Terrain not initialized");
            return;
        }

        auto &config = terrainComp.generator.getConfigMutable();

        float heightScale = config.heightScale;
        if (ImGui::DragFloat("Height Scale", &heightScale, 1.0f, 0.0f, 1000.0f)) {
            terrainComp.generator.setHeightScale(heightScale);
        }

        ImGui::DragFloat("Chunk Size", &config.chunkWorldSize, 1.0f, 1.0f, 256.0f);
        ImGui::DragFloat("Terrain Size", &config.terrainWorldSize, 10.0f, 64.0f, 8192.0f);

        ImGui::Separator();
        ImGui::Text("Loaded Chunks: %zu", terrainComp.generator.getLoadedChunkCount());
        ImGui::Text("Visible Chunks: %u", terrainComp.generator.getTotalVisibleChunks());

        for (uint32_t lod = 0; lod < Rapture::TERRAIN_LOD_COUNT; ++lod) {
            uint32_t count = terrainComp.generator.getVisibleChunkCount(lod);
            if (count > 0) {
                ImGui::Text("  LOD%u: %u chunks", lod, count);
            }
        }

        bool wireframe = terrainComp.generator.isWireframe();
        if (ImGui::Checkbox("Wireframe", &wireframe)) {
            terrainComp.generator.setWireframe(wireframe);
        }

        ImGui::Separator();

        auto &multiNoise = terrainComp.generator.getMultiNoiseConfig();
        bool splineChanged = false;

        const char *categoryNames[] = {"Continentalness", "Erosion", "Peaks & Valleys"};

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
            terrainComp.generator.bakeNoiseLUT();
        }

        ImGui::Separator();
    }
}
