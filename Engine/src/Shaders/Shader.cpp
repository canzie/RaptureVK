#include "Shader.h"

#include "Buffers/Descriptors/DescriptorManager.h"
#include "Logging/Log.h"
#include "Utils/io.h"
#include "WindowContext/Application.h"

#include "ShaderReflections.h"

#include <algorithm>

namespace Rapture {

// Legacy constructor: vertex + fragment
Shader::Shader(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath, ShaderCompileInfo compileInfo)
{
    m_compileInfo = compileInfo;

    addStage(ShaderType::VERTEX, vertexPath);
    if (!fragmentPath.empty()) {
        addStage(ShaderType::FRAGMENT, fragmentPath);
    }

    if (!build()) {
        m_status = ShaderStatus::FAILED;
    }
}

// Legacy constructor: compute
Shader::Shader(const std::filesystem::path &computePath, ShaderCompileInfo compileInfo)
{
    m_compileInfo = compileInfo;

    if (!computePath.empty()) {
        addStage(ShaderType::COMPUTE, computePath);
        if (!build()) {
            m_status = ShaderStatus::FAILED;
        }
    }
}

Shader::~Shader()
{
    cleanup();
}

Shader::Shader(Shader &&other) noexcept
    : m_stages(std::move(other.m_stages)), m_compileInfo(std::move(other.m_compileInfo)), m_status(other.m_status),
      m_pipelineStages(std::move(other.m_pipelineStages)), m_descriptorSetInfos(std::move(other.m_descriptorSetInfos)),
      m_descriptorSetLayouts(std::move(other.m_descriptorSetLayouts)),
      m_pushConstantLayouts(std::move(other.m_pushConstantLayouts)),
      m_detailedPushConstants(std::move(other.m_detailedPushConstants)), m_materialSets(std::move(other.m_materialSets))
{
    other.m_status = ShaderStatus::UNINITIALIZED;
    other.m_descriptorSetLayouts.clear();
}

Shader &Shader::operator=(Shader &&other) noexcept
{
    if (this != &other) {
        cleanup();

        m_stages = std::move(other.m_stages);
        m_compileInfo = std::move(other.m_compileInfo);
        m_status = other.m_status;
        m_pipelineStages = std::move(other.m_pipelineStages);
        m_descriptorSetInfos = std::move(other.m_descriptorSetInfos);
        m_descriptorSetLayouts = std::move(other.m_descriptorSetLayouts);
        m_pushConstantLayouts = std::move(other.m_pushConstantLayouts);
        m_detailedPushConstants = std::move(other.m_detailedPushConstants);
        m_materialSets = std::move(other.m_materialSets);

        other.m_status = ShaderStatus::UNINITIALIZED;
        other.m_descriptorSetLayouts.clear();
    }
    return *this;
}

void Shader::cleanup()
{
    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    for (auto &layout : m_descriptorSetLayouts) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
    }
    m_descriptorSetLayouts.clear();

    for (auto &stage : m_stages) {
        if (stage.module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, stage.module, nullptr);
            stage.module = VK_NULL_HANDLE;
        }
    }

    m_pipelineStages.clear();
}

Shader &Shader::addStage(ShaderType type, const std::filesystem::path &path)
{
    // Check for duplicate stage
    if (hasStage(type)) {
        RP_CORE_WARN("Shader stage {} already added, replacing", shaderTypeToString(type));
    }

    // Remove existing stage of same type
    m_stages.erase(std::remove_if(m_stages.begin(), m_stages.end(), [type](const ShaderStage &s) { return s.type == type; }),
                   m_stages.end());

    ShaderStage stage;
    stage.type = type;
    stage.sourcePath = path;
    m_stages.push_back(stage);

    if (m_status == ShaderStatus::UNINITIALIZED) {
        m_status = ShaderStatus::STAGES_ADDED;
    }

    return *this;
}

Shader &Shader::setCompileInfo(const ShaderCompileInfo &info)
{
    m_compileInfo = info;
    return *this;
}

bool Shader::hasStage(ShaderType type) const
{
    for (const auto &stage : m_stages) {
        if (stage.type == type) {
            return true;
        }
    }
    return false;
}

const VkShaderModule &Shader::getModule(ShaderType type) const
{
    static VkShaderModule nullModule = VK_NULL_HANDLE;
    for (const auto &stage : m_stages) {
        if (stage.type == type) {
            return stage.module;
        }
    }
    return nullModule;
}

bool Shader::compileStage(ShaderStage &stage)
{
    if (stage.sourcePath.extension() == ".spv") {
        stage.spirv = readFile(stage.sourcePath);
    } else {
        stage.spirv = m_compiler.Compile(stage.sourcePath, m_compileInfo);
    }

    if (stage.spirv.empty()) {
        RP_CORE_ERROR("Failed to compile shader: {}", stage.sourcePath.string());
        return false;
    }

    // Create VkShaderModule
    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = stage.spirv.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(stage.spirv.data());

    if (vkCreateShaderModule(device, &createInfo, nullptr, &stage.module) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create shader module: {}", stage.sourcePath.string());
        return false;
    }

    return true;
}

void Shader::reflectStage(const ShaderStage &stage)
{
    VkShaderStageFlags stageFlags = shaderTypeToVkStage(stage.type);

    const uint32_t *spirvData = reinterpret_cast<const uint32_t *>(stage.spirv.data());
    size_t spirvSize = stage.spirv.size();

    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(spirvSize, spirvData, &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        RP_CORE_ERROR("Failed to create reflection data for stage {}", shaderTypeToString(stage.type));
        return;
    }

    // Reflect descriptor bindings
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

            // Find or create set info
            auto it = std::find_if(m_descriptorSetInfos.begin(), m_descriptorSetInfos.end(),
                                   [setNumber](const DescriptorSetInfo &info) { return info.setNumber == setNumber; });

            if (it == m_descriptorSetInfos.end()) {
                m_descriptorSetInfos.push_back(DescriptorSetInfo{setNumber, {}});
                it = m_descriptorSetInfos.end() - 1;
            }

            // Check if binding already exists (merge stage flags)
            auto bindIt = std::find_if(it->bindings.begin(), it->bindings.end(),
                                       [&](const DescriptorBindingInfo &b) { return b.binding == bindingInfo.binding; });

            if (bindIt != it->bindings.end()) {
                bindIt->stageFlags |= stageFlags;
            } else {
                it->bindings.push_back(bindingInfo);
            }
        }
    }

    spvReflectDestroyShaderModule(&module);
}

void Shader::mergeReflectionData()
{
    // Sort sets by number
    std::sort(m_descriptorSetInfos.begin(), m_descriptorSetInfos.end(),
              [](const DescriptorSetInfo &a, const DescriptorSetInfo &b) { return a.setNumber < b.setNumber; });

    // Sort bindings within each set
    for (auto &setInfo : m_descriptorSetInfos) {
        std::sort(setInfo.bindings.begin(), setInfo.bindings.end(),
                  [](const DescriptorBindingInfo &a, const DescriptorBindingInfo &b) { return a.binding < b.binding; });
    }

    // Extract push constants from all stages
    std::vector<std::pair<std::vector<char>, VkShaderStageFlags>> stageSpirvs;
    for (const auto &stage : m_stages) {
        stageSpirvs.push_back({stage.spirv, shaderTypeToVkStage(stage.type)});
    }

    std::vector<PushConstantInfo> pushConstantInfos = getCombinedPushConstantRanges(stageSpirvs);
    m_pushConstantLayouts = pushConstantInfoToRanges(pushConstantInfos);

    // Extract detailed push constants from first stage that has them
    for (const auto &stage : m_stages) {
        auto detailed = extractDetailedPushConstants(stage.spirv);
        if (!detailed.empty()) {
            m_detailedPushConstants = detailed;
            break;
        }
    }

    // Extract material sets
    for (const auto &stage : m_stages) {
        std::vector<DescriptorInfo> stageMaterialSets = extractMaterialSets(stage.spirv);
        for (const auto &matSet : stageMaterialSets) {
            auto it = std::find_if(m_materialSets.begin(), m_materialSets.end(), [&](const DescriptorInfo &existing) {
                return existing.setNumber == matSet.setNumber && existing.binding == matSet.binding;
            });
            if (it == m_materialSets.end()) {
                m_materialSets.push_back(matSet);
            }
        }
    }

    RP_CORE_INFO("Shader reflection data:");
    // printDescriptorSetInfos(m_descriptorSetInfos);
    // printPushConstantLayouts(pushConstantInfos);
}

void Shader::buildPipelineStages()
{
    m_pipelineStages.clear();

    for (const auto &stage : m_stages) {
        if (!stage.hasModule()) continue;

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = shaderTypeToVkStage(stage.type);
        stageInfo.module = stage.module;
        stageInfo.pName = "main";
        stageInfo.pSpecializationInfo = nullptr;

        m_pipelineStages.push_back(stageInfo);
    }
}

bool Shader::compile()
{
    if (m_stages.empty()) {
        RP_CORE_ERROR("No shader stages added");
        m_status = ShaderStatus::FAILED;
        return false;
    }

    // Compile all stages
    for (auto &stage : m_stages) {
        if (!compileStage(stage)) {
            m_status = ShaderStatus::FAILED;
            return false;
        }
    }

    // Reflect all stages
    m_descriptorSetInfos.clear();
    for (const auto &stage : m_stages) {
        reflectStage(stage);
    }

    // Merge reflection data
    mergeReflectionData();

    // Build pipeline stage infos
    buildPipelineStages();

    m_status = ShaderStatus::COMPILED;
    return true;
}

bool Shader::createDescriptorLayouts()
{
    if (m_status < ShaderStatus::COMPILED) {
        RP_CORE_ERROR("Cannot create descriptor layouts before compiling");
        return false;
    }

    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    // Clear existing layouts
    for (auto &layout : m_descriptorSetLayouts) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
    }
    m_descriptorSetLayouts.clear();

    // Determine max set number
    uint32_t maxSetNumber = 0;
    for (const auto &setInfo : m_descriptorSetInfos) {
        maxSetNumber = std::max(maxSetNumber, setInfo.setNumber);
    }

    m_descriptorSetLayouts.resize(maxSetNumber + 1, VK_NULL_HANDLE);

    // Process each set
    for (uint32_t setNumber = 0; setNumber <= maxSetNumber; ++setNumber) {
        std::shared_ptr<DescriptorSet> descriptorSet = nullptr;

        // Sets 0-3 are managed by DescriptorManager
        if (setNumber <= 3) {
            descriptorSet = DescriptorManager::getDescriptorSet(setNumber);
        }

        if (descriptorSet) {
            m_descriptorSetLayouts[setNumber] = descriptorSet->getLayout();
        } else {
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
        RP_CORE_WARN("No descriptor set layouts created - shader might not use any descriptors");
    }

    m_status = ShaderStatus::READY;
    return true;
}

void Shader::createDescriptorSetLayoutFromInfo(const DescriptorSetInfo &setInfo)
{
    Application &app = Application::getInstance();
    VkDevice device = app.getVulkanContext().getLogicalDevice();

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(setInfo.bindings.size());
    std::vector<VkDescriptorBindingFlags> bindingFlags(setInfo.bindings.size(), VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);

    for (const auto &bindingInfo : setInfo.bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = bindingInfo.binding;
        layoutBinding.descriptorType = bindingInfo.descriptorType;
        layoutBinding.descriptorCount = bindingInfo.descriptorCount;
        layoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
        layoutBinding.pImmutableSamplers = nullptr;

        layoutBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        RP_CORE_ERROR("Failed to create descriptor set layout for set {}!", setInfo.setNumber);
        return;
    }

    if (m_descriptorSetLayouts.size() <= setInfo.setNumber) {
        m_descriptorSetLayouts.resize(setInfo.setNumber + 1, VK_NULL_HANDLE);
    }
    m_descriptorSetLayouts[setInfo.setNumber] = layout;
}

bool Shader::build()
{
    if (!compile()) {
        return false;
    }

    if (!createDescriptorLayouts()) {
        return false;
    }

    return true;
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
    case ShaderType::TESSELLATION_CONTROL:
        return "TESSELLATION_CONTROL";
    case ShaderType::TESSELLATION_EVALUATION:
        return "TESSELLATION_EVALUATION";
    case ShaderType::MESH:
        return "MESH";
    case ShaderType::TASK:
        return "TASK";
    default:
        return "UNKNOWN";
    }
}

namespace {
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

std::string shaderStageFlagsToString(VkShaderStageFlags flags)
{
    std::string result;
    if (flags & VK_SHADER_STAGE_VERTEX_BIT) result += "VERTEX | ";
    if (flags & VK_SHADER_STAGE_FRAGMENT_BIT) result += "FRAGMENT | ";
    if (flags & VK_SHADER_STAGE_COMPUTE_BIT) result += "COMPUTE | ";
    if (flags & VK_SHADER_STAGE_GEOMETRY_BIT) result += "GEOMETRY | ";
    if (flags & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) result += "TESS_CONTROL | ";
    if (flags & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) result += "TESS_EVAL | ";
    if (flags & VK_SHADER_STAGE_MESH_BIT_EXT) result += "MESH | ";
    if (flags & VK_SHADER_STAGE_TASK_BIT_EXT) result += "TASK | ";

    if (result.empty()) return "NONE";
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
