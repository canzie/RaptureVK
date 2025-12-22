#include "Shader.h"

#include "Buffers/Descriptors/DescriptorManager.h"
#include "Logging/Log.h"
#include "Utils/io.h"
#include "WindowContext/Application.h"

#include "ShaderReflections.h"

namespace Rapture {

Shader::Shader(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath, ShaderCompileInfo compileInfo)
    : m_compileInfo(compileInfo)
{

    if (fragmentPath.empty()) {
        createGraphicsShader(vertexPath);
    } else {
        createGraphicsShader(vertexPath, fragmentPath);
    }
    createDescriptorSetLayout();

    // Test SPIRV-Reflect library by reflecting on our shaders
    RP_CORE_INFO("Testing SPIRV-Reflect functionality:");
    printDescriptorSetInfos(m_descriptorSetInfos);
}

Shader::Shader(const std::filesystem::path &computePath, ShaderCompileInfo compileInfo) : m_compileInfo(compileInfo)
{

    if (computePath.empty()) {
        return;
    }

    if (computePath.extension() == ".glsl") {
        // compileShaderToSPIRV
    }

    createComputeShader(computePath);
    createDescriptorSetLayout();

    // Test SPIRV-Reflect library by reflecting on our shaders
    RP_CORE_INFO("Testing SPIRV-Reflect functionality for compute shader:");
    printDescriptorSetInfos(m_descriptorSetInfos);
}

Shader::~Shader()
{
    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    for (auto &descriptorSetLayout : m_descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }

    for (auto &source : m_sources) {
        vkDestroyShaderModule(device, source.second, nullptr);
    }
}

void Shader::createGraphicsShader(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath)
{
    std::vector<char> vertexCode;
    if (vertexPath.extension() == ".spv") vertexCode = readFile(vertexPath);
    else vertexCode = m_compiler.Compile(vertexPath, m_compileInfo);

    std::vector<char> fragmentCode;
    if (!fragmentPath.empty()) {
        if (fragmentPath.extension() == ".spv") fragmentCode = readFile(fragmentPath);
        else fragmentCode = m_compiler.Compile(fragmentPath, m_compileInfo);
    }

    // Collect descriptor information before creating shader modules
    m_descriptorSetInfos = collectDescriptorSetInfo(vertexCode, fragmentCode);

    createShaderModule(vertexCode, ShaderType::VERTEX);
    createShaderModule(fragmentCode, ShaderType::FRAGMENT);

    m_materialSets = extractMaterialSets(vertexCode);
    std::vector<DescriptorInfo> fragmentMaterialSets = extractMaterialSets(fragmentCode);

    std::vector<PushConstantInfo> pushConstantInfos =
        getCombinedPushConstantRanges({{vertexCode, VK_SHADER_STAGE_VERTEX_BIT}, {fragmentCode, VK_SHADER_STAGE_FRAGMENT_BIT}});
    m_pushConstantLayouts = pushConstantInfoToRanges(pushConstantInfos);

    // Print push constant reflection data
    RP_CORE_INFO("Push Constant Reflection Data:");
    printPushConstantLayouts(pushConstantInfos);

    // Helper lambda to check if a descriptor is already in the material sets
    auto isDescriptorDuplicate = [](const std::vector<DescriptorInfo> &sets, const DescriptorInfo &info) {
        return std::find_if(sets.begin(), sets.end(), [&info](const DescriptorInfo &existing) {
                   return existing.setNumber == info.setNumber && existing.binding == info.binding;
               }) != sets.end();
    };

    // Add fragment material sets, skipping duplicates
    for (const auto &fragmentSet : fragmentMaterialSets) {
        if (!isDescriptorDuplicate(m_materialSets, fragmentSet)) {
            m_materialSets.push_back(fragmentSet);
        }
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = m_sources[ShaderType::VERTEX];
    vertShaderStageInfo.pName = "main";
    vertShaderStageInfo.pSpecializationInfo = nullptr; // constants i think idk

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = m_sources[ShaderType::FRAGMENT];
    fragShaderStageInfo.pName = "main";
    fragShaderStageInfo.pSpecializationInfo = nullptr; // constants i think idk

    m_stages.push_back(vertShaderStageInfo);
    m_stages.push_back(fragShaderStageInfo);
}

void Shader::createGraphicsShader(const std::filesystem::path &vertexPath)
{

    std::vector<char> vertexCode;
    if (vertexPath.extension() == ".spv") vertexCode = readFile(vertexPath);
    else vertexCode = m_compiler.Compile(vertexPath, m_compileInfo);

    // Collect descriptor information before creating shader modules
    m_descriptorSetInfos = collectDescriptorSetInfo(vertexCode, {});

    createShaderModule(vertexCode, ShaderType::VERTEX);

    std::vector<PushConstantInfo> pushConstantInfos = getCombinedPushConstantRanges({{vertexCode, VK_SHADER_STAGE_VERTEX_BIT}});
    m_pushConstantLayouts = pushConstantInfoToRanges(pushConstantInfos);

    // Print push constant reflection data
    RP_CORE_INFO("Push Constant Reflection Data:");
    printPushConstantLayouts(pushConstantInfos);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = m_sources[ShaderType::VERTEX];
    vertShaderStageInfo.pName = "main";
    vertShaderStageInfo.pSpecializationInfo = nullptr; // constants i think idk

    m_stages.push_back(vertShaderStageInfo);
}

void Shader::createComputeShader(const std::filesystem::path &computePath)
{
    std::vector<char> computeCode;
    if (computePath.extension() == ".spv") computeCode = readFile(computePath);
    else computeCode = m_compiler.Compile(computePath, m_compileInfo);

    // Collect descriptor information before creating shader modules
    m_descriptorSetInfos = collectDescriptorSetInfo({}, computeCode);

    createShaderModule(computeCode, ShaderType::COMPUTE);

    // Extract push constants
    std::vector<PushConstantInfo> pushConstantInfos = getCombinedPushConstantRanges({{computeCode, VK_SHADER_STAGE_COMPUTE_BIT}});
    m_pushConstantLayouts = pushConstantInfoToRanges(pushConstantInfos);

    // Print push constant reflection data
    RP_CORE_INFO("Compute Shader Push Constant Reflection Data:");
    printPushConstantLayouts(pushConstantInfos);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = m_sources[ShaderType::COMPUTE];
    computeShaderStageInfo.pName = "main";
    computeShaderStageInfo.pSpecializationInfo = nullptr; // constants i think idk

    m_stages.push_back(computeShaderStageInfo);
}

void Shader::createShaderModule(const std::vector<char> &code, ShaderType type)
{

    if (m_sources.find(type) != m_sources.end()) {
        RP_CORE_WARN("shader module of type {0} already exists! overwriting...", shaderTypeToString(type));
    }

    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        RP_CORE_ERROR("failed to create shader module!");
        throw std::runtime_error("Shader::createShaderModule - failed to create shader module!");
    }

    m_sources[type] = shaderModule;
}

void Shader::createDescriptorSetLayout()
{
    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    // Clear any existing layouts
    for (auto &layout : m_descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
    }
    m_descriptorSetLayouts.clear();

    // First, determine the maximum set number we need
    uint32_t maxSetNumber = 0;
    for (const auto &setInfo : m_descriptorSetInfos) {
        maxSetNumber = std::max(maxSetNumber, setInfo.setNumber);
    }

    m_descriptorSetLayouts.resize(maxSetNumber + 1, VK_NULL_HANDLE);

    // Process each set
    for (uint32_t setNumber = 0; setNumber <= maxSetNumber; ++setNumber) {
        std::shared_ptr<DescriptorSet> descriptorSet = nullptr;

        // Sets 0-3 are considered "managed" and can be retrieved from the DescriptorManager.
        if (setNumber <= 3) {
            descriptorSet = DescriptorManager::getDescriptorSet(setNumber);
        }

        if (descriptorSet) {
            m_descriptorSetLayouts[setNumber] = descriptorSet->getLayout();
        } else {
            // For sets > 3, or if a managed set is not available, we fall back to creating the layout from reflection data.
            if (setNumber <= 3) {
                RP_CORE_WARN("DescriptorManager set {} not available, falling back to shader layout", setNumber);
            }

            auto it = std::find_if(m_descriptorSetInfos.begin(), m_descriptorSetInfos.end(),
                                   [setNumber](const DescriptorSetInfo &info) { return info.setNumber == setNumber; });

            if (it != m_descriptorSetInfos.end()) {
                createDescriptorSetLayoutFromInfo(*it);
            }
        }
    }

    if (m_descriptorSetLayouts.empty()) {
        RP_CORE_WARN("No descriptor set layouts were created - shader might not use any descriptors");
    }
}

void Shader::createDescriptorSetLayoutFromInfo(const DescriptorSetInfo &setInfo)
{
    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(setInfo.bindings.size());
    std::vector<VkDescriptorBindingFlags> bindingFlags(setInfo.bindings.size(), VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);

    // each binding in a set
    for (const auto &bindingInfo : setInfo.bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = bindingInfo.binding;
        layoutBinding.descriptorType = bindingInfo.descriptorType;
        layoutBinding.descriptorCount = bindingInfo.descriptorCount;
        layoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
        layoutBinding.pImmutableSamplers = nullptr;

        layoutBindings.push_back(layoutBinding);
    }

    // Set up binding flags info
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT; // Required for UPDATE_AFTER_BIND
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    // layout for all bindings in a set
    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create descriptor set layout for set {0}!", setInfo.setNumber);
        throw std::runtime_error("Failed to create descriptor set layout!");
    }

    // Ensure we have space in our layout vector for this set number
    if (m_descriptorSetLayouts.size() <= setInfo.setNumber) {
        m_descriptorSetLayouts.resize(setInfo.setNumber + 1, VK_NULL_HANDLE);
    }
    m_descriptorSetLayouts[setInfo.setNumber] = layout;
}

std::vector<DescriptorSetInfo> Shader::collectDescriptorSetInfo(const std::vector<char> &vertexSpirv,
                                                                const std::vector<char> &fragmentSpirv)
{
    std::map<uint32_t, DescriptorSetInfo> setInfoMap; // Map set number to DescriptorSetInfo

    // Helper lambda to process shader module
    auto processShaderModule = [&](const std::vector<char> &spirv, VkShaderStageFlags stageFlags) {
        const uint32_t *spirvData = reinterpret_cast<const uint32_t *>(spirv.data());
        size_t spirvSize = spirv.size();

        SpvReflectShaderModule module;
        SpvReflectResult result = spvReflectCreateShaderModule(spirvSize, spirvData, &module);
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            RP_CORE_ERROR("Failed to create reflection data for shader stage {0}!", stageFlags);
            return;
        }

        // Get descriptor bindings
        uint32_t count = 0;
        result = spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
        if (result == SPV_REFLECT_RESULT_SUCCESS && count > 0) {
            std::vector<SpvReflectDescriptorBinding *> bindings(count);
            result = spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

            for (auto binding : bindings) {
                DescriptorBindingInfo bindingInfo{};
                bindingInfo.binding = binding->binding;
                bindingInfo.descriptorType = static_cast<VkDescriptorType>(binding->descriptor_type);
                bindingInfo.descriptorCount = binding->count;
                bindingInfo.stageFlags = stageFlags;
                bindingInfo.name = binding->name ? binding->name : "unnamed";

                uint32_t setNumber = binding->set;

                // Create or get the set info
                if (setInfoMap.find(setNumber) == setInfoMap.end()) {
                    setInfoMap[setNumber] = DescriptorSetInfo{setNumber, {}};
                }

                // Check if this binding already exists
                auto &existingBindings = setInfoMap[setNumber].bindings;
                auto it =
                    std::find_if(existingBindings.begin(), existingBindings.end(),
                                 [&](const DescriptorBindingInfo &existing) { return existing.binding == bindingInfo.binding; });

                if (it != existingBindings.end()) {
                    // Binding exists, merge stage flags
                    it->stageFlags |= stageFlags;
                } else {
                    // New binding
                    existingBindings.push_back(bindingInfo);
                }
            }
        }

        spvReflectDestroyShaderModule(&module);
    };

    // Process shader stages
    if (!vertexSpirv.empty()) {
        processShaderModule(vertexSpirv, VK_SHADER_STAGE_VERTEX_BIT);
    }

    if (!fragmentSpirv.empty()) {
        if (vertexSpirv.empty()) {
            // This is a compute shader (compute code passed as fragmentSpirv)
            processShaderModule(fragmentSpirv, VK_SHADER_STAGE_COMPUTE_BIT);
        } else {
            // This is a fragment shader
            processShaderModule(fragmentSpirv, VK_SHADER_STAGE_FRAGMENT_BIT);
        }
    }

    // Convert map to vector, sorted by set number
    std::vector<DescriptorSetInfo> result;
    result.reserve(setInfoMap.size());
    for (auto &[setNumber, setInfo] : setInfoMap) {
        // Sort bindings by binding number for consistency
        std::sort(setInfo.bindings.begin(), setInfo.bindings.end(),
                  [](const DescriptorBindingInfo &a, const DescriptorBindingInfo &b) { return a.binding < b.binding; });
        result.push_back(std::move(setInfo));
    }

    // Sort by set number
    std::sort(result.begin(), result.end(),
              [](const DescriptorSetInfo &a, const DescriptorSetInfo &b) { return a.setNumber < b.setNumber; });

    return result;
}

const VkShaderModule &Shader::getSource(ShaderType type)
{
    return m_sources[type];
}

std::string shaderTypeToString(ShaderType type)
{
    switch (type) {
    case ShaderType::VERTEX:
        return "VERTEX";
    case ShaderType::FRAGMENT:
        return "FRAGMENT";
    case ShaderType::GEOMETRY:
        return "GEOMETRY";
    case ShaderType::COMPUTE:
        return "COMPUTE";
    default:
        return "UNKNOWN";
    }
}

namespace {
// Helper function to convert VkDescriptorType to string
std::string descriptorTypeToString(VkDescriptorType type)
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        return "SAMPLER";
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return "COMBINED_IMAGE_SAMPLER";
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return "SAMPLED_IMAGE";
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return "STORAGE_IMAGE";
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return "UNIFORM_TEXEL_BUFFER";
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return "STORAGE_TEXEL_BUFFER";
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return "UNIFORM_BUFFER";
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return "STORAGE_BUFFER";
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        return "UNIFORM_BUFFER_DYNAMIC";
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return "STORAGE_BUFFER_DYNAMIC";
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return "INPUT_ATTACHMENT";
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        return "ACCELERATION_STRUCTURE_KHR";
    default:
        return "UNKNOWN";
    }
}

// Helper function to convert shader stage flags to string
std::string shaderStageFlagsToString(VkShaderStageFlags flags)
{
    std::string result;
    if (flags & VK_SHADER_STAGE_VERTEX_BIT) result += "VERTEX | ";
    if (flags & VK_SHADER_STAGE_FRAGMENT_BIT) result += "FRAGMENT | ";
    if (flags & VK_SHADER_STAGE_COMPUTE_BIT) result += "COMPUTE | ";
    if (flags & VK_SHADER_STAGE_GEOMETRY_BIT) result += "GEOMETRY | ";
    if (flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) result += "TESS_CONTROL | ";
    if (flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) result += "TESS_EVAL | ";

    if (result.empty()) return "NONE";
    // Remove trailing " | "
    return result.substr(0, result.length() - 3);
}
} // namespace

void printDescriptorSetInfo(const DescriptorSetInfo &setInfo)
{
    RP_CORE_INFO("Descriptor Set {0}:", setInfo.setNumber);

    if (setInfo.bindings.empty()) {
        RP_CORE_INFO("  No bindings in this set");
        return;
    }

    for (const auto &binding : setInfo.bindings) {
        RP_CORE_INFO("\t Binding {0}:", binding.binding);
        RP_CORE_INFO("\t\t Name: {0}", binding.name);
        RP_CORE_INFO("\t\t Type: {0}", descriptorTypeToString(binding.descriptorType));
        RP_CORE_INFO("\t\t Count: {0}", binding.descriptorCount);
        RP_CORE_INFO("\t\t Stages: {0}", shaderStageFlagsToString(binding.stageFlags));
    }
}

void printDescriptorSetInfos(const std::vector<DescriptorSetInfo> &setInfos)
{
    if (setInfos.empty()) {
        RP_CORE_INFO("No descriptor sets found in shader");
        return;
    }

    RP_CORE_INFO("Found {0} descriptor set(s):", setInfos.size());
    for (const auto &setInfo : setInfos) {
        printDescriptorSetInfo(setInfo);
    }
}

void printPushConstantLayout(const PushConstantInfo &pushConstantInfo)
{
    RP_CORE_INFO("Push Constant Block:");
    RP_CORE_INFO("\t Name: {0}", pushConstantInfo.name);
    RP_CORE_INFO("\t Offset: {0} bytes", pushConstantInfo.offset);
    RP_CORE_INFO("\t Size: {0} bytes", pushConstantInfo.size);
    RP_CORE_INFO("\t Stages: {0}", shaderStageFlagsToString(pushConstantInfo.stageFlags));
}

void printPushConstantLayouts(const std::vector<PushConstantInfo> &pushConstantInfos)
{
    if (pushConstantInfos.empty()) {
        RP_CORE_INFO("No push constants found in shader");
        return;
    }

    RP_CORE_INFO("Found {0} push constant block(s):", pushConstantInfos.size());
    for (const auto &pushConstantInfo : pushConstantInfos) {
        printPushConstantLayout(pushConstantInfo);
    }
}

} // namespace Rapture