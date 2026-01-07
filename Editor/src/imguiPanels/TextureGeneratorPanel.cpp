#include "TextureGeneratorPanel.h"

#include "AssetManager/AssetManager.h"
#include "Shaders/ShaderReflections.h"
#include "WindowContext/Application.h"

#include <cfloat>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <filesystem>

struct ParameterEditState {
    float floatValue = 1.0f;
    int intValue = 1;
    uint32_t uintValue = 1;
    glm::vec2 vec2Value = glm::vec2(1.0f);
    glm::vec3 vec3Value = glm::vec3(1.0f);
    glm::vec4 vec4Value = glm::vec4(1.0f);
    Rapture::PushConstantMemberInfo::BaseType activeType;

    void initFromMetadata(const Rapture::PushConstantMemberMetadata &metadata)
    {
        if (!metadata.hasDefault || metadata.defaultValue.empty()) {
            return;
        }
        const auto &def = metadata.defaultValue;
        switch (activeType) {
        case Rapture::PushConstantMemberInfo::BaseType::FLOAT:
            floatValue = def[0];
            break;
        case Rapture::PushConstantMemberInfo::BaseType::INT:
            intValue = static_cast<int>(def[0]);
            break;
        case Rapture::PushConstantMemberInfo::BaseType::UINT:
            uintValue = static_cast<uint32_t>(def[0]);
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC2:
            vec2Value = glm::vec2(def.size() > 0 ? def[0] : 0.0f, def.size() > 1 ? def[1] : 0.0f);
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC3:
            vec3Value = glm::vec3(def.size() > 0 ? def[0] : 0.0f, def.size() > 1 ? def[1] : 0.0f, def.size() > 2 ? def[2] : 0.0f);
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC4:
            vec4Value = glm::vec4(def.size() > 0 ? def[0] : 0.0f, def.size() > 1 ? def[1] : 0.0f, def.size() > 2 ? def[2] : 0.0f,
                                  def.size() > 3 ? def[3] : 0.0f);
            break;
        default:
            break;
        }
    }

    void loadFromBuffer(const uint8_t *buffer, const Rapture::PushConstantMemberInfo &member)
    {
        const uint8_t *src = buffer + member.offset;
        switch (activeType) {
        case Rapture::PushConstantMemberInfo::BaseType::FLOAT:
            std::memcpy(&floatValue, src, sizeof(float));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::INT:
            std::memcpy(&intValue, src, sizeof(int));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::UINT:
            std::memcpy(&uintValue, src, sizeof(uint32_t));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC2:
            std::memcpy(&vec2Value, src, sizeof(glm::vec2));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC3:
            std::memcpy(&vec3Value, src, sizeof(glm::vec3));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC4:
            std::memcpy(&vec4Value, src, sizeof(glm::vec4));
            break;
        default:
            break;
        }
    }

    void writeToBuffer(uint8_t *buffer, const Rapture::PushConstantMemberInfo &member) const
    {
        uint8_t *dest = buffer + member.offset;
        switch (activeType) {
        case Rapture::PushConstantMemberInfo::BaseType::FLOAT:
            std::memcpy(dest, &floatValue, sizeof(float));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::INT:
            std::memcpy(dest, &intValue, sizeof(int));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::UINT:
            std::memcpy(dest, &uintValue, sizeof(uint32_t));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC2:
            std::memcpy(dest, &vec2Value, sizeof(glm::vec2));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC3:
            std::memcpy(dest, &vec3Value, sizeof(glm::vec3));
            break;
        case Rapture::PushConstantMemberInfo::BaseType::VEC4:
            std::memcpy(dest, &vec4Value, sizeof(glm::vec4));
            break;
        default:
            break;
        }
    }
};

struct TextureGeneratorInstance {
    std::string name;
    std::string shaderPath;
    Rapture::AssetHandle shaderHandle;
    std::vector<uint8_t> buffer;
    std::vector<ParameterEditState> editStates;
    Rapture::ProceduralTextureConfig config;
    std::shared_ptr<Rapture::ProceduralTexture> generator;
    bool isDirty = false;
    bool autoUpdate = false;
};

TextureGeneratorPanel::TextureGeneratorPanel() {}

TextureGeneratorPanel::~TextureGeneratorPanel() {}

void TextureGeneratorPanel::render()
{
    ImGui::Begin("Texture Generator");

    renderInstanceSelector();
    ImGui::Separator();

    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_instances.size())) {
        renderParameterEditor();
        ImGui::Separator();
        renderGenerateButton();
    }

    ImGui::End();
}

void TextureGeneratorPanel::renderInstanceSelector()
{
    ImGui::Text("Instance:");
    ImGui::SameLine();

    std::string previewText = m_selectedIndex < 0 ? "Select or Create New" : m_instances[m_selectedIndex].name;

    if (ImGui::BeginCombo("##InstanceSelect", previewText.c_str())) {
        for (size_t i = 0; i < m_instances.size(); ++i) {
            bool isSelected = (m_selectedIndex == static_cast<int>(i));
            if (ImGui::Selectable(m_instances[i].name.c_str(), isSelected)) {
                m_selectedIndex = static_cast<int>(i);
            }
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("New")) {
        ImGui::OpenPopup("CreateNewInstance");
    }

    if (ImGui::BeginPopup("CreateNewInstance")) {
        ImGui::Text("Select shader:");

        const auto &loadedAssets = Rapture::AssetManager::getLoadedAssets();
        const auto &assetRegistry = Rapture::AssetManager::getAssetRegistry();

        for (const auto &[handle, asset_] : loadedAssets) {
            if (assetRegistry.find(handle) == assetRegistry.end()) continue;

            auto &metadata = Rapture::AssetManager::getAssetMetadata(handle);
            if (metadata.assetType != Rapture::AssetType::SHADER) continue;

            auto asset = Rapture::AssetManager::getAsset(handle);
            auto shader = asset ? asset.get()->getUnderlyingAsset<Rapture::Shader>() : nullptr;
            if (!shader || !shader->isReady()) continue;
            const std::string &name = metadata.isDiskAsset() ? metadata.filePath.filename().string() : metadata.virtualName;

            if (ImGui::Selectable(name.c_str())) {
                TextureGeneratorInstance newInstance;
                newInstance.name = name + " #" + std::to_string(m_instances.size() + 1);
                newInstance.shaderHandle = handle;

                Rapture::ProceduralTextureConfig config;
                config.format = Rapture::TextureFormat::RGBA16F;
                config.filter = Rapture::TextureFilter::Linear;
                config.wrap = Rapture::TextureWrap::Repeat;
                config.srgb = false;
                config.name = name;

                newInstance.generator = std::make_shared<Rapture::ProceduralTexture>(handle, config);
                newInstance.config = config;

                if (newInstance.generator->isValid()) {
                    const auto &detailedPc = shader->getDetailedPushConstants();

                    if (!detailedPc.empty()) {
                        const auto &pcInfo = detailedPc[0];
                        newInstance.buffer.resize(pcInfo.size, 0);

                        for (const auto &member : pcInfo.members) {
                            ParameterEditState editState;
                            editState.activeType = member.getBaseType();
                            editState.initFromMetadata(member.metadata);
                            newInstance.editStates.push_back(editState);
                            editState.writeToBuffer(newInstance.buffer.data(), member);
                        }
                    }

                    m_instances.push_back(newInstance);
                    m_selectedIndex = static_cast<int>(m_instances.size() - 1);
                }

                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    if (m_selectedIndex >= 0 && ImGui::Button("Delete")) {
        m_instances.erase(m_instances.begin() + m_selectedIndex);
        m_selectedIndex = -1;
    }
}

void TextureGeneratorPanel::renderParameterEditor()
{
    auto &instance = m_instances[m_selectedIndex];

    if (!instance.generator || !instance.generator->isValid()) {
        ImGui::TextColored(ImVec4(1, 0.3, 0.3, 1), "Invalid generator");
        return;
    }

    auto &shader = instance.generator->getShader();
    if (!shader.isReady()) {
        ImGui::TextColored(ImVec4(1, 0.3, 0.3, 1), "Shader not ready");
        return;
    }

    const auto &detailedPc = shader.getDetailedPushConstants();
    if (detailedPc.empty()) {
        ImGui::Text("No parameters");
        return;
    }

    ImGui::Text("Parameters:");

    for (const auto &pcInfo : detailedPc) {
        if (ImGui::TreeNodeEx(pcInfo.blockName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t memberIdx = 0; memberIdx < pcInfo.members.size(); ++memberIdx) {
                const auto &member = pcInfo.members[memberIdx];
                const auto &metadata = member.metadata;

                if (metadata.hidden) {
                    continue;
                }

                if (memberIdx >= instance.editStates.size()) {
                    continue;
                }

                auto &editState = instance.editStates[memberIdx];
                bool changed = false;
                const char *label = metadata.displayName.empty() ? member.name.c_str() : metadata.displayName.c_str();

                float minVal = metadata.hasRange ? metadata.minValue : -FLT_MAX;
                float maxVal = metadata.hasRange ? metadata.maxValue : FLT_MAX;
                float speed = metadata.hasRange ? (metadata.maxValue - metadata.minValue) * 0.01f : 0.1f;

                ImGui::PushID(static_cast<int>(memberIdx));

                switch (editState.activeType) {
                case Rapture::PushConstantMemberInfo::BaseType::FLOAT:
                    if (metadata.hasRange) {
                        changed = ImGui::SliderFloat(label, &editState.floatValue, minVal, maxVal);
                    } else {
                        changed = ImGui::DragFloat(label, &editState.floatValue, speed);
                    }
                    break;
                case Rapture::PushConstantMemberInfo::BaseType::INT:
                    if (metadata.hasRange) {
                        changed = ImGui::SliderInt(label, &editState.intValue, static_cast<int>(minVal), static_cast<int>(maxVal));
                    } else {
                        changed = ImGui::DragInt(label, &editState.intValue);
                    }
                    break;
                case Rapture::PushConstantMemberInfo::BaseType::UINT:
                    changed = ImGui::DragScalar(label, ImGuiDataType_U32, &editState.uintValue);
                    break;
                case Rapture::PushConstantMemberInfo::BaseType::VEC2:
                    if (metadata.hasRange) {
                        changed = ImGui::SliderFloat2(label, glm::value_ptr(editState.vec2Value), minVal, maxVal);
                    } else {
                        changed = ImGui::DragFloat2(label, glm::value_ptr(editState.vec2Value), speed);
                    }
                    break;
                case Rapture::PushConstantMemberInfo::BaseType::VEC3:
                    if (metadata.isColor) {
                        changed = ImGui::ColorEdit3(label, glm::value_ptr(editState.vec3Value));
                    } else if (metadata.hasRange) {
                        changed = ImGui::SliderFloat3(label, glm::value_ptr(editState.vec3Value), minVal, maxVal);
                    } else {
                        changed = ImGui::DragFloat3(label, glm::value_ptr(editState.vec3Value), speed);
                    }
                    break;
                case Rapture::PushConstantMemberInfo::BaseType::VEC4:
                    if (metadata.isColor) {
                        changed = ImGui::ColorEdit4(label, glm::value_ptr(editState.vec4Value));
                    } else if (metadata.hasRange) {
                        changed = ImGui::SliderFloat4(label, glm::value_ptr(editState.vec4Value), minVal, maxVal);
                    } else {
                        changed = ImGui::DragFloat4(label, glm::value_ptr(editState.vec4Value), speed);
                    }
                    break;
                default:
                    ImGui::BeginDisabled();
                    ImGui::Text("%s: (unsupported type: %s)", label, member.type.c_str());
                    ImGui::EndDisabled();
                    break;
                }

                ImGui::PopID();

                if (changed) {
                    instance.isDirty = true;
                    editState.writeToBuffer(instance.buffer.data(), member);
                }
            }

            ImGui::TreePop();
        }
    }
}

void TextureGeneratorPanel::renderGenerateButton()
{

    auto &instance = m_instances[m_selectedIndex];
    if (instance.generator && instance.generator->isValid()) {

        ImGui::Checkbox("Auto Update", &instance.autoUpdate);

        if (ImGui::Button("Generate") || (instance.isDirty && instance.autoUpdate)) {
            if (!instance.buffer.empty()) {
                instance.generator->setPushConstantsRaw(instance.buffer.data(), instance.buffer.size());
            }
            instance.generator->generate();
            instance.isDirty = false;
        }
    }
}
