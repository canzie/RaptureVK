#include "ShaderReflections.h"
#include "Logging/Log.h"

#include "Shaders/Shader.h"

#include <algorithm>
#include <map>
#include <utility> // For std::pair

namespace Rapture {

void reflectShaderInfo(const std::vector<char> &spirvCode)
{
    // The SPIR-V binary is already in the code vector
    const uint32_t *spirvData = reinterpret_cast<const uint32_t *>(spirvCode.data());
    size_t spirvSize = spirvCode.size();

    // Create reflection data from SPIR-V
    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(spirvSize, spirvData, &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        RP_CORE_ERROR("Failed to create reflection data for shader!");
        return;
    }

    // Extract descriptor bindings
    uint32_t count = 0;
    result = spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    if (result == SPV_REFLECT_RESULT_SUCCESS && count > 0) {
        std::vector<SpvReflectDescriptorBinding *> bindings(count);
        result = spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

        RP_CORE_INFO("  Descriptor Bindings ({0}):", count);
        for (uint32_t i = 0; i < count; ++i) {
            auto &binding = *bindings[i];
            RP_CORE_INFO("    Binding {0}: set={1}, type={2}, name={3}", binding.binding, binding.set,
                         descriptorTypeToString(binding.descriptor_type), binding.name ? binding.name : "unnamed");
        }
    }

    // Extract input variables
    count = 0;
    result = spvReflectEnumerateInputVariables(&module, &count, nullptr);
    if (result == SPV_REFLECT_RESULT_SUCCESS && count > 0) {
        std::vector<SpvReflectInterfaceVariable *> inputs(count);
        result = spvReflectEnumerateInputVariables(&module, &count, inputs.data());

        RP_CORE_INFO("  Input Variables ({0}):", count);
        for (uint32_t i = 0; i < count; ++i) {
            auto &input = *inputs[i];
            RP_CORE_INFO("    Location {0}: name={1}, format={2}", input.location, input.name ? input.name : "unnamed", "FORMAT");
        }
    }

    // Extract output variables
    count = 0;
    result = spvReflectEnumerateOutputVariables(&module, &count, nullptr);
    if (result == SPV_REFLECT_RESULT_SUCCESS && count > 0) {
        std::vector<SpvReflectInterfaceVariable *> outputs(count);
        result = spvReflectEnumerateOutputVariables(&module, &count, outputs.data());

        RP_CORE_INFO("  Output Variables ({0}):", count);
        for (uint32_t i = 0; i < count; ++i) {
            auto &output = *outputs[i];
            RP_CORE_INFO("    Location {0}: name={1}, format={2}", output.location, output.name ? output.name : "unnamed",
                         "FORMAT");
        }
    }

    // Extract push constants
    count = 0;
    result = spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr);
    if (result == SPV_REFLECT_RESULT_SUCCESS && count > 0) {
        std::vector<SpvReflectBlockVariable *> pushConstants(count);
        result = spvReflectEnumeratePushConstantBlocks(&module, &count, pushConstants.data());

        RP_CORE_INFO("  Push Constants ({0}):", count);
        for (uint32_t i = 0; i < count; ++i) {
            auto &pushConstant = *pushConstants[i];
            RP_CORE_INFO("    Size: {0} bytes, name={1}", pushConstant.size, pushConstant.name ? pushConstant.name : "unnamed");
        }
    }

    // Clean up
    spvReflectDestroyShaderModule(&module);
}

// New implementation for getSpirvTypeDescriptionString
std::string getSpirvTypeDescriptionString(const SpvReflectTypeDescription *typeDescription)
{
    if (!typeDescription) {
        return "unknown_type_description_null";
    }

    // Check for a predefined type name first (e.g., for structs)
    if (typeDescription->type_name) {
        return typeDescription->type_name;
    }

    // Handle built-in SPIR-V types
    switch (typeDescription->op) {
    case SpvOpTypeVoid:
        return "void";
    case SpvOpTypeBool:
        return "bool";
    case SpvOpTypeInt:
        if (typeDescription->traits.numeric.scalar.signedness) {
            return "int";
        } else {
            return "uint";
        }
    case SpvOpTypeFloat:
        if (typeDescription->traits.numeric.scalar.width == 32) {
            return "float";
        } else if (typeDescription->traits.numeric.scalar.width == 64) {
            return "double";
        }
        break; // Fallthrough to more generic type construction
    case SpvOpTypeVector: {
        std::string componentType = "unknown_vector_component";
        if (typeDescription->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) componentType = "float";
        else if (typeDescription->type_flags & SPV_REFLECT_TYPE_FLAG_INT) {
            // Further check for signedness if possible, though SpvReflectTypeDescription
            // doesn't directly expose signedness for vector components in a simple way.
            // Assuming 'int' for simplicity here. GLSL often defaults to signed int for 'ivec'.
            // If UBO members are unsigned, they should ideally be uint in GLSL.
            if (typeDescription->members && typeDescription->members[0].type_flags & SPV_REFLECT_TYPE_FLAG_INT &&
                typeDescription->members[0].traits.numeric.scalar.signedness == 0) {
                componentType = "uint";
            } else {
                componentType = "int";
            }

        } else if (typeDescription->type_flags & SPV_REFLECT_TYPE_FLAG_BOOL) componentType = "bool";

        std::string prefix;
        if (componentType == "float") prefix = "vec";
        else if (componentType == "int") prefix = "ivec";
        else if (componentType == "uint") prefix = "uvec";
        else if (componentType == "bool") prefix = "bvec";
        else prefix = "vec"; // Default prefix

        return prefix + std::to_string(typeDescription->traits.numeric.vector.component_count);
    }
    case SpvOpTypeMatrix: {
        // Assuming float matrices (matNxM) as they are most common.
        // SpvReflect does not easily give column/row component types for matrices directly.
        // We'd typically look at the type of the vector that makes up the columns.
        // For simplicity, we'll assume 'float' components.
        std::string prefix = "mat";
        // SPIR-V matrices are column-major. traits.numeric.matrix.column_count is columns, .row_count is rows.
        // GLSL mat notation is mat<cols>x<rows> or mat<N> for square.
        if (typeDescription->traits.numeric.matrix.column_count == typeDescription->traits.numeric.matrix.row_count) {
            return prefix + std::to_string(typeDescription->traits.numeric.matrix.column_count);
        } else {
            return prefix + std::to_string(typeDescription->traits.numeric.matrix.column_count) + "x" +
                   std::to_string(typeDescription->traits.numeric.matrix.row_count);
        }
    }
    // Add other types as needed (e.g., SpvOpTypeImage, SpvOpTypeSampler, etc.)
    default:
        // Try to build a generic name if possible, or return a placeholder
        if (typeDescription->type_flags & SPV_REFLECT_TYPE_FLAG_STRUCT) {
            return "struct"; // Generic struct if no name
        }
        // Fallback if no specific handling
        return "unhandled_spirv_op_" + std::to_string(typeDescription->op);
    }
    return "unknown_type_fallback"; // Fallback for unhandled float/int widths or other cases
}

bool isTextureDescriptorType(SpvReflectDescriptorType descriptorType)
{
    switch (descriptorType) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return true;
    default:
        return false;
    }
}

std::string descriptorTypeToString(SpvReflectDescriptorType descriptorType)
{
    switch (descriptorType) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        return "SAMPLER";
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return "COMBINED_IMAGE_SAMPLER";
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return "SAMPLED_IMAGE";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return "STORAGE_IMAGE";
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return "UNIFORM_TEXEL_BUFFER";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return "STORAGE_TEXEL_BUFFER";
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return "UNIFORM_BUFFER";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return "STORAGE_BUFFER";
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        return "UNIFORM_BUFFER_DYNAMIC";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return "STORAGE_BUFFER_DYNAMIC";
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return "INPUT_ATTACHMENT";
    case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        return "ACCELERATION_STRUCTURE_KHR";
    default:
        return "UNKNOWN";
    }
}

std::vector<DescriptorInfo> extractMaterialSets(const std::vector<char> &spirvCode)
{
    std::vector<DescriptorInfo> result;
    // Create SPIR-V reflection module
    const uint32_t *spirvData = reinterpret_cast<const uint32_t *>(spirvCode.data());

    size_t spirvSize = spirvCode.size();
    SpvReflectShaderModule module;
    SpvReflectResult reflectResult = spvReflectCreateShaderModule(spirvSize, spirvData, &module);

    if (reflectResult != SPV_REFLECT_RESULT_SUCCESS) {
        RP_CORE_ERROR("Failed to create reflection data for material extraction!");
        return result;
    } // Enumerate descriptor bindings

    uint32_t count = 0;
    reflectResult = spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    if (reflectResult != SPV_REFLECT_RESULT_SUCCESS || count == 0) {
        spvReflectDestroyShaderModule(&module);
        return result;
    }

    std::vector<SpvReflectDescriptorBinding *> bindings(count);
    reflectResult = spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
    if (reflectResult != SPV_REFLECT_RESULT_SUCCESS) {
        spvReflectDestroyShaderModule(&module);
        return result;
    } // Filter for MATERIAL set descriptors only

    for (uint32_t i = 0; i < count; ++i) {
        auto &binding = *bindings[i]; // Only process descriptors in the MATERIAL set (set index 1)

        if (binding.set != static_cast<uint32_t>(DESCRIPTOR_SET_INDICES::MATERIAL)) {
            continue;
        }

        DescriptorInfo descriptorInfo;
        descriptorInfo.name = binding.name ? binding.name : "unnamed";
        descriptorInfo.binding = binding.binding;
        descriptorInfo.setNumber = binding.set;
        // Handle uniform buffers with members
        if (binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER && binding.block.member_count > 0) {
            // Process each member of the uniform buffer
            for (uint32_t memberIdx = 0; memberIdx < binding.block.member_count; ++memberIdx) {
                auto &member = binding.block.members[memberIdx];
                DescriptorParamInfo paramInfo;
                paramInfo.name = member.name ? member.name : "unnamed";
                paramInfo.type = getSpirvTypeDescriptionString(member.type_description); // Use the new helper
                paramInfo.size = member.size;
                paramInfo.offset = member.offset;
                descriptorInfo.params.push_back(paramInfo);
            }

        } else { // For non-uniform buffer descriptors (textures, samplers, etc.)
            // Create a single parameter representing the entire descriptor
            DescriptorParamInfo paramInfo;
            paramInfo.name = descriptorInfo.name;
            paramInfo.type = descriptorTypeToString(binding.descriptor_type);
            paramInfo.size = 0;   // Not applicable for textures/samplers
            paramInfo.offset = 0; // Not applicable for textures/samplers
            descriptorInfo.params.push_back(paramInfo);
        }

        result.push_back(descriptorInfo);
    }

    // Clean up
    spvReflectDestroyShaderModule(&module);
    return result;
}

std::vector<PushConstantInfo>
getCombinedPushConstantRanges(const std::vector<std::pair<const std::vector<char> &, VkShaderStageFlags>> &shaderCodeWithStages)
{
    // Use a map to merge push constant ranges by offset and size, combining stage flags.
    // The key is a pair of {offset, size}, the value is PushConstantInfo.
    std::map<std::pair<uint32_t, uint32_t>, PushConstantInfo> mergedPushConstants;

    for (const auto &shaderDataPair : shaderCodeWithStages) {
        const std::vector<char> &spirvCode = shaderDataPair.first;
        VkShaderStageFlags stageHint = shaderDataPair.second; // Hint for the primary stage of this SPIR-V

        const uint32_t *spirvData = reinterpret_cast<const uint32_t *>(spirvCode.data());
        size_t spirvSize = spirvCode.size();

        SpvReflectShaderModule module;
        SpvReflectResult result = spvReflectCreateShaderModule(spirvSize, spirvData, &module);
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            RP_CORE_ERROR("Failed to create reflection data for shader stage (hint: {0}) for push constants!", stageHint);
            // Optionally, continue to process other shaders or return an empty vector
            continue;
        }

        uint32_t count = 0;
        result = spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr);
        if (result == SPV_REFLECT_RESULT_SUCCESS && count > 0) {
            std::vector<SpvReflectBlockVariable *> spvPushConstants(count);
            result = spvReflectEnumeratePushConstantBlocks(&module, &count, spvPushConstants.data());

            if (result == SPV_REFLECT_RESULT_SUCCESS) {
                for (const auto *spvPcBlock : spvPushConstants) {
                    if (!spvPcBlock) continue;

                    // SPIR-V Reflect gives the shader stage for the *module* itself.
                    // If a push constant is truly used by multiple stages, it will appear in multiple modules.
                    VkShaderStageFlags actualStageFlags = module.shader_stage;
                    if (actualStageFlags == 0) { // If module.shader_stage is not specific, use the hint.
                        actualStageFlags = stageHint;
                    }

                    std::pair<uint32_t, uint32_t> key = {spvPcBlock->offset, spvPcBlock->size};
                    std::string name = spvPcBlock->name ? spvPcBlock->name : "unnamed_push_constant";

                    auto it = mergedPushConstants.find(key);
                    if (it != mergedPushConstants.end()) {
                        // This push constant (offset, size) already exists, merge stage flags
                        it->second.stageFlags |= actualStageFlags;
                        // Optional: Name merging strategy (e.g., if names differ, append or log)
                        if (it->second.name == "unnamed_push_constant" && name != "unnamed_push_constant") {
                            it->second.name = name; // Prefer a non-default name
                        } else if (it->second.name != name && name != "unnamed_push_constant") {
                            // You might want a more sophisticated naming strategy for conflicts
                            RP_CORE_WARN(
                                "Push constant at offset {0}, size {1} has conflicting names: '{2}' and '{3}'. Using '{2}'.",
                                key.first, key.second, it->second.name, name);
                        }
                    } else {
                        // New push constant range
                        PushConstantInfo pcInfo;
                        pcInfo.offset = spvPcBlock->offset;
                        pcInfo.size = spvPcBlock->size;
                        pcInfo.stageFlags = actualStageFlags;
                        pcInfo.name = name;
                        mergedPushConstants[key] = pcInfo;
                    }
                }
            }
        }
        spvReflectDestroyShaderModule(&module);
    }

    // Convert map to vector
    std::vector<PushConstantInfo> resultVector;
    resultVector.reserve(mergedPushConstants.size());
    for (const auto &pair : mergedPushConstants) {
        resultVector.push_back(pair.second);
    }

    // Sort by offset for predictable order, then by size as a secondary criterion.
    std::sort(resultVector.begin(), resultVector.end(), [](const PushConstantInfo &a, const PushConstantInfo &b) {
        if (a.offset != b.offset) {
            return a.offset < b.offset;
        }
        return a.size < b.size; // Could also sort by name or stageFlags if needed for tie-breaking
    });

    return resultVector;
}

} // namespace Rapture