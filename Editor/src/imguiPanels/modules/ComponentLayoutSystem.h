#ifndef RAPTURE__COMPONENT_LAYOUT_SYSTEM_H
#define RAPTURE__COMPONENT_LAYOUT_SYSTEM_H

#include "ScratchBuffer.h"
#include <functional>
#include <glm/glm.hpp>
#include <imgui.h>
#include <string>
#include <vector>

namespace ComponentUI {

enum class FieldType {
    NONE,
    FLOAT,
    INT,
    BOOL,
    VEC2,
    VEC3,
    VEC4,
    COLOR3,
    COLOR4,
    ENUM,
    STRING,
    MATERIAL,
    TEXTURE,
};

enum class WidgetType {
    NONE,
    DRAG,         // DragFloat/DragInt
    SLIDER,       // SliderFloat/SliderInt
    INPUT,        // InputFloat/InputInt
    CHECKBOX,     // For bool
    COMBO,        // For enums
    COLOR_EDIT,   // ColorEdit3/4
    ASSET_PICKER, // For materials/textures with preview + dropdown
};

enum FieldFlags {
    NONE = 0,
    LOCKED = 1 << 0, // Field is locked (read-only), can be toggled at runtime
};

// Type-specific options
struct FloatOptions {
    float speed = 0.1f;
    float min = 0.0f;
    float max = 0.0f; // 0 = no limit
    const char *format = "%.3f";
};

struct IntOptions {
    int speed = 1;
    int min = 0;
    int max = 0; // 0 = no limit
};

struct Vec3Options {
    float speed = 0.1f;
    const char *format = "%.3f";
};

struct EnumOptions {
    const char **names = nullptr;
    int count = 0;
};

// For materials and textures - provides asset listing and preview
struct AssetOptions {
    // Lambda to get list of available asset handles
    std::function<std::vector<uint64_t>()> getAvailableAssets;

    // Lambda to get asset name from handle
    std::function<std::string(uint64_t)> getAssetName;

    // Lambda to get preview descriptor set (for textures/materials)
    std::function<void *(uint64_t)> getPreviewDescriptor;

    // Preview size (if applicable)
    float previewSize = 32.0f;
};

// Separator descriptor
struct SeparatorDescriptor {
    enum Type {
        LINE,    // ImGui::Separator()
        SPACING, // ImGui::Spacing()
        DUMMY,   // ImGui::Dummy()
        TEXT,    // ImGui::Text()
    };

    Type type;
    const char *text = nullptr; // For TEXT type
    float height = 0.0f;        // For DUMMY type

    static SeparatorDescriptor Line() { return {LINE}; }
    static SeparatorDescriptor Spacing() { return {SPACING}; }
    static SeparatorDescriptor Dummy(float height) { return {DUMMY, nullptr, height}; }
    static SeparatorDescriptor Text(const char *text) { return {TEXT, text}; }
};

template <typename ComponentType> struct FieldDescriptor {
    const char *name = nullptr;
    FieldType type = FieldType::NONE;
    WidgetType widget = WidgetType::NONE;
    int flags = 0;

    std::function<void *(ComponentType &, ScratchBuffer &)> accessor = nullptr;
    std::function<void(void *, ComponentType &)> onChange = nullptr;
    void *options = nullptr;

    FieldDescriptor(const char *name, FieldType type, WidgetType widget,
                    std::function<void *(ComponentType &, ScratchBuffer &)> accessor, int flags = NONE, void *options = nullptr,
                    std::function<void(void *, ComponentType &)> onChange = nullptr)
        : name(name), type(type), widget(widget), flags(flags), accessor(accessor), options(options), onChange(onChange)
    {
    }
    FieldDescriptor() {}
};

// Layout element - either a field or separator
template <typename ComponentType> struct LayoutElement {
    enum class Type {
        FIELD,
        SEPARATOR
    };

    Type type;
    FieldDescriptor<ComponentType> field;
    SeparatorDescriptor separator;

    static LayoutElement Field(FieldDescriptor<ComponentType> field)
    {
        return LayoutElement<ComponentType>(Type::FIELD, field, {});
    }

    static LayoutElement Separator(SeparatorDescriptor sep)
    {
        return LayoutElement<ComponentType>(Type::SEPARATOR, FieldDescriptor<ComponentType>{}, sep);
    }

  private:
    LayoutElement(Type type, FieldDescriptor<ComponentType> field, SeparatorDescriptor sep)
        : type(type), field(field), separator(sep)
    {
    }
};

// Complete component layout
template <typename ComponentType> struct ComponentLayout {
    const char *componentName;
    std::vector<LayoutElement<ComponentType>> elements;
};

template <typename ComponentType>
bool renderComponentLayout(const ComponentLayout<ComponentType> &layout, ComponentType &component, ScratchBuffer &scratch)
{
    bool anyChanged = false;
    if (ImGui::CollapsingHeader(layout.componentName, ImGuiTreeNodeFlags_DefaultOpen)) {

        ImGui::BeginTable(layout.componentName, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp);

        for (const auto &element : layout.elements) {
            if (element.type == LayoutElement<ComponentType>::Type::SEPARATOR) {
                // End table, render separator, restart table
                ImGui::EndTable();

                switch (element.separator.type) {
                case SeparatorDescriptor::LINE:
                    ImGui::Separator();
                    break;
                case SeparatorDescriptor::SPACING:
                    ImGui::Spacing();
                    break;
                case SeparatorDescriptor::DUMMY:
                    ImGui::Dummy(ImVec2(0.0f, element.separator.height));
                    break;
                case SeparatorDescriptor::TEXT:
                    ImGui::Text("%s", element.separator.text);
                    break;
                }

                ImGui::BeginTable(layout.componentName, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp);
                continue;
            }

            const auto &field = element.field;
            void *valuePtr = field.accessor(component, scratch);

            if (!valuePtr) continue;

            bool locked = field.flags & LOCKED;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", field.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);

            std::string label = std::string("##") + field.name;

            if (locked) {
                ImGui::BeginDisabled();
            }

            bool changed = false;

            switch (field.type) {
            case FieldType::FLOAT: {
                float *value = static_cast<float *>(valuePtr);
                FloatOptions *opts = field.options ? static_cast<FloatOptions *>(field.options) : nullptr;
                float speed = opts ? opts->speed : 0.1f;
                float min = opts ? opts->min : 0.0f;
                float max = opts ? opts->max : 0.0f;
                const char *format = opts ? opts->format : "%.3f";

                switch (field.widget) {
                case WidgetType::DRAG:
                    changed = ImGui::DragFloat(label.c_str(), value, speed, min, max, format);
                    break;
                case WidgetType::SLIDER:
                    changed = ImGui::SliderFloat(label.c_str(), value, min, max, format);
                    break;
                case WidgetType::INPUT:
                    changed = ImGui::InputFloat(label.c_str(), value, 0, 0, format);
                    break;
                default:
                    changed = ImGui::DragFloat(label.c_str(), value, speed, min, max, format);
                    break;
                }
                break;
            }

            case FieldType::INT: {
                int *value = static_cast<int *>(valuePtr);
                IntOptions *opts = field.options ? static_cast<IntOptions *>(field.options) : nullptr;
                int speed = opts ? opts->speed : 1;
                int min = opts ? opts->min : 0;
                int max = opts ? opts->max : 0;

                switch (field.widget) {
                case WidgetType::DRAG:
                    changed = ImGui::DragInt(label.c_str(), value, static_cast<float>(speed), min, max);
                    break;
                case WidgetType::SLIDER:
                    changed = ImGui::SliderInt(label.c_str(), value, min, max);
                    break;
                case WidgetType::INPUT:
                    changed = ImGui::InputInt(label.c_str(), value);
                    break;
                default:
                    changed = ImGui::DragInt(label.c_str(), value, static_cast<float>(speed), min, max);
                    break;
                }
                break;
            }

            case FieldType::BOOL: {
                bool *value = static_cast<bool *>(valuePtr);
                changed = ImGui::Checkbox(label.c_str(), value);
                break;
            }

            case FieldType::VEC3: {
                glm::vec3 *value = static_cast<glm::vec3 *>(valuePtr);
                Vec3Options *opts = field.options ? static_cast<Vec3Options *>(field.options) : nullptr;
                float speed = opts ? opts->speed : 0.1f;
                const char *format = opts ? opts->format : "%.3f";

                changed = ImGui::DragFloat3(label.c_str(), &value->x, speed, 0.0f, 0.0f, format);
                break;
            }

            case FieldType::VEC4: {
                glm::vec4 *value = static_cast<glm::vec4 *>(valuePtr);
                Vec3Options *opts = field.options ? static_cast<Vec3Options *>(field.options) : nullptr;
                float speed = opts ? opts->speed : 0.1f;
                const char *format = opts ? opts->format : "%.3f";

                changed = ImGui::DragFloat4(label.c_str(), &value->x, speed, 0.0f, 0.0f, format);
                break;
            }

            case FieldType::COLOR3: {
                glm::vec3 *value = static_cast<glm::vec3 *>(valuePtr);
                changed = ImGui::ColorEdit3(label.c_str(), &value->x);
                break;
            }

            case FieldType::COLOR4: {
                glm::vec4 *value = static_cast<glm::vec4 *>(valuePtr);
                changed = ImGui::ColorEdit4(label.c_str(), &value->x);
                break;
            }

            case FieldType::ENUM: {
                int *value = static_cast<int *>(valuePtr);
                EnumOptions *opts = static_cast<EnumOptions *>(field.options);
                changed = ImGui::Combo(label.c_str(), value, opts->names, opts->count);
                break;
            }

            case FieldType::MATERIAL:
            case FieldType::TEXTURE: {
                uint64_t *assetHandle = static_cast<uint64_t *>(valuePtr);
                AssetOptions *opts = static_cast<AssetOptions *>(field.options);

                if (!opts) break;

                void *previewDescriptor = opts->getPreviewDescriptor(*assetHandle);

                if (previewDescriptor) {
                    ImGui::Image(reinterpret_cast<ImTextureID>(previewDescriptor), ImVec2(opts->previewSize, opts->previewSize));
                    ImGui::SameLine();
                }

                std::string currentName = opts->getAssetName(*assetHandle);
                if (currentName.empty()) {
                    currentName = "None";
                }

                if (ImGui::BeginCombo(label.c_str(), currentName.c_str())) {
                    auto availableAssets = opts->getAvailableAssets();

                    for (uint64_t handle : availableAssets) {
                        std::string assetName = opts->getAssetName(handle);
                        bool isSelected = (*assetHandle == handle);

                        if (ImGui::Selectable(assetName.c_str(), isSelected)) {
                            *assetHandle = handle;
                            changed = true;
                        }

                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    ImGui::EndCombo();
                }
                break;
            }

            default:
                ImGui::Text("Unsupported type");
                break;
            }

            if (locked) {
                ImGui::EndDisabled();
            }

            if (changed && field.onChange) {
                field.onChange(valuePtr, component);
            }
            if (changed) {
                anyChanged = true;
            }
        }

        ImGui::EndTable();
    }
    return anyChanged;
}

} // namespace ComponentUI

#endif // RAPTURE__COMPONENT_LAYOUT_SYSTEM_H
