#ifndef RAPTURE__SHADER_REFLECTIONS_INTERNAL_H
#define RAPTURE__SHADER_REFLECTIONS_INTERNAL_H

#include <spirv_reflect.h>
#include <string>

namespace Rapture {

// Helper to determine if a descriptor type is a texture
bool isTextureDescriptorType(SpvReflectDescriptorType descriptorType);

// Convert SPIR-V descriptor type to string (for debugging)
std::string descriptorTypeToString(SpvReflectDescriptorType descriptorType);

// New helper function to get a string representation of a SPIR-V type description
std::string getSpirvTypeDescriptionString(const SpvReflectTypeDescription *typeDescription);

} // namespace Rapture

#endif // RAPTURE__SHADER_REFLECTIONS_INTERNAL_H
