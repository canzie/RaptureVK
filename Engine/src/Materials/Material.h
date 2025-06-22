#pragma once

#include "MaterialParameters.h"

#include <memory>
#include "Pipelines/GraphicsPipeline.h"

#include "Buffers/Descriptors/DescriptorArrayManager.h"
#include "Buffers/Buffers.h"
#include "Textures/Texture.h"

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
        static std::unique_ptr<DescriptorSubAllocationBase<Buffer>> s_bindlessUniformBuffers;
        // manages by the texture class
        // i would love to use a seoerate allocation for the material textures but i just dont know how to prevent
        // multiple materials that use the same texture to not recreate a new bindless index
        // one possiblity is to tell the texture about some index AND tell it that it does not own it/does not belong to its allocation
        //static std::unique_ptr<DescriptorSubAllocationBase<Texture>> s_bindlessTextures;


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

