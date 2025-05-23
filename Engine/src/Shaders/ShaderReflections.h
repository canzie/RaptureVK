#pragma once

#include <vector>
#include <spirv_reflect.h>


namespace Rapture {

    struct DescriptorParamInfo {
        std::string name;
        std::string type;
        uint32_t size;
        uint32_t offset;
    };

    struct DescriptorInfo {
        std::string name;
        uint32_t setNumber;
        uint32_t binding;
        std::vector<DescriptorParamInfo> params;
    };

    // Reflect and log shader information
    static void reflectShaderInfo(const std::vector<char>& spirvCode);
    
    // Helper to determine if a descriptor type is a texture
    bool isTextureDescriptorType(SpvReflectDescriptorType descriptorType);
    
    // Convert SPIR-V descriptor type to string (for debugging)
    std::string descriptorTypeToString(SpvReflectDescriptorType descriptorType);

    // Get descriptor info from the material set
    std::vector<DescriptorInfo> extractMaterialSets(const std::vector<char>& spirvCode);

    // New helper function to get a string representation of a SPIR-V type description
    std::string getSpirvTypeDescriptionString(const SpvReflectTypeDescription* typeDescription);

}
