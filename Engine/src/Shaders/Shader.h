#ifndef RAPTURE__SHADER_H
#define RAPTURE__SHADER_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Buffers/Buffers.h"
#include "ShaderCommon.h"
#include "ShaderCompilation.h"
#include "ShaderReflections.h"
#include <vulkan/vulkan.h>

#include <spirv_reflect.h>

namespace Rapture {

enum class ShaderStatus {
    UNINITIALIZED,
    STAGES_ADDED,
    COMPILED, // SPIRV generated, modules created, reflected
    READY,    // Descriptor layouts created
    FAILED
};

/**
 * @brief Represents a single shader stage (vertex, fragment, compute, mesh, etc.)
 */
struct ShaderStage {
    ShaderType type;
    std::filesystem::path sourcePath;
    std::vector<char> spirv;
    VkShaderModule module = VK_NULL_HANDLE;

    bool isCompiled() const { return !spirv.empty(); }
    bool hasModule() const { return module != VK_NULL_HANDLE; }
};

void printDescriptorSetInfo(const DescriptorSetInfo &setInfo);
void printDescriptorSetInfos(const std::vector<DescriptorSetInfo> &setInfos);

void printPushConstantLayout(const PushConstantInfo &pushConstantInfo);
void printPushConstantLayouts(const std::vector<PushConstantInfo> &pushConstantInfos);

std::string shaderTypeToString(ShaderType type);

/**
 * @brief Shader class with explicit stage management and separated compilation.
 *
 * Usage:
 *   // Graphics shader
 *   Shader shader;
 *   shader.addStage(ShaderType::VERTEX, "shaders/terrain.vs.glsl")
 *         .addStage(ShaderType::FRAGMENT, "shaders/terrain.fs.glsl")
 *         .setCompileInfo({.includePath = "shaders/"});
 *   if (!shader.build()) { handle error }
 *
 *   // Compute shader
 *   Shader compute;
 *   compute.addStage(ShaderType::COMPUTE, "shaders/noise.cs.glsl").build();
 *
 *   // Mesh shader pipeline
 *   Shader meshShader;
 *   meshShader.addStage(ShaderType::TASK, "shaders/terrain.task.glsl")
 *             .addStage(ShaderType::MESH, "shaders/terrain.mesh.glsl")
 *             .addStage(ShaderType::FRAGMENT, "shaders/terrain.fs.glsl")
 *             .build();
 */
class Shader {
  public:
    Shader() = default;

    // Legacy constructors for backward compatibility
    Shader(const std::filesystem::path &vertexPath, const std::filesystem::path &fragmentPath, ShaderCompileInfo compileInfo = {});
    Shader(const std::filesystem::path &computePath, ShaderCompileInfo compileInfo = {});

    ~Shader();

    // Non-copyable
    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    // Movable
    Shader(Shader &&other) noexcept;
    Shader &operator=(Shader &&other) noexcept;

    // Stage management (fluent API)
    Shader &addStage(ShaderType type, const std::filesystem::path &path);
    Shader &setCompileInfo(const ShaderCompileInfo &info);

    // Build steps
    bool compile();                 // Compile SPIRV, create modules, reflect data
    bool createDescriptorLayouts(); // Create descriptor set layouts (call after compile)

    // Convenience: compile() + createDescriptorLayouts()
    bool build();

    // State queries
    bool isCompiled() const { return m_status >= ShaderStatus::COMPILED; }
    bool isReady() const { return m_status == ShaderStatus::READY; }
    ShaderStatus getStatus() const { return m_status; }
    bool hasStage(ShaderType type) const;

    // Getters
    const std::vector<VkPipelineShaderStageCreateInfo> &getStages() const { return m_pipelineStages; }
    const VkShaderModule &getModule(ShaderType type) const;

    const std::vector<VkDescriptorSetLayout> &getDescriptorSetLayouts() const { return m_descriptorSetLayouts; }
    const std::vector<DescriptorSetInfo> &getDescriptorSetInfos() const { return m_descriptorSetInfos; }
    const std::vector<DescriptorInfo> &getMaterialSets() const { return m_materialSets; }

    const std::vector<VkPushConstantRange> &getPushConstantLayouts() const { return m_pushConstantLayouts; }
    const std::vector<DetailedPushConstantInfo> &getDetailedPushConstants() const { return m_detailedPushConstants; }

    // Get raw stages for inspection
    const std::vector<ShaderStage> &getShaderStages() const { return m_stages; }

  private:
    void cleanup();
    bool compileStage(ShaderStage &stage);
    void reflectStage(const ShaderStage &stage);
    void mergeReflectionData();
    void createDescriptorSetLayoutFromInfo(const DescriptorSetInfo &setInfo);
    void buildPipelineStages();

    std::vector<ShaderStage> m_stages;
    ShaderCompileInfo m_compileInfo;
    ShaderCompiler m_compiler;
    ShaderStatus m_status = ShaderStatus::UNINITIALIZED;

    // Pipeline stage info (built after createModules)
    std::vector<VkPipelineShaderStageCreateInfo> m_pipelineStages;

    // Merged reflection data
    std::vector<DescriptorSetInfo> m_descriptorSetInfos;
    std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
    std::vector<VkPushConstantRange> m_pushConstantLayouts;
    std::vector<DetailedPushConstantInfo> m_detailedPushConstants;
    std::vector<DescriptorInfo> m_materialSets;
};

} // namespace Rapture

#endif // RAPTURE__SHADER_H
