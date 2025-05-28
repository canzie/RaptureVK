#pragma once

#include "MaterialParameters.h"

#include <memory>
#include "Pipelines/GraphicsPipeline.h"

namespace Rapture {

// use old parameter system ish
// use reflections to get the types and names
// use predifined comparisons for mapping like:
//   PARAM_ID : ["albedo", "albedo_color", "color", "diffuse", ...]
// just make sure to check for lowercase/uppercase 

class BaseMaterial : public std::enable_shared_from_this<BaseMaterial> {
    public:
        BaseMaterial(std::shared_ptr<Shader> shader, const std::string& name = "");
        ~BaseMaterial() = default;



        const std::unordered_map<ParameterID, MaterialParameter>& getTemplateParameters() const { return m_parameterMap; }

        uint32_t getSizeBytes() const { return m_sizeBytes; }
        std::string getName() const { return m_name; }
        VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }

    protected:
    
    private:
        std::string m_name;
        uint32_t m_sizeBytes;
        VkDescriptorSetLayout m_descriptorSetLayout;
        std::shared_ptr<Shader> m_shader;

        std::unordered_map<ParameterID, MaterialParameter> m_parameterMap;

    friend class MaterialInstance;
    friend class MaterialManager;
};


class MaterialManager {
    public:
        static void init();
        static void shutdown();

        static std::shared_ptr<BaseMaterial> getMaterial(const std::string& name);
        static void printMaterialNames();
    private:
        static bool s_initialized;
        static std::unordered_map<std::string, std::shared_ptr<BaseMaterial>> s_materials;
};

}

