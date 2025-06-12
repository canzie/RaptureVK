#include "DynamicDiffuseGI.h"

#include "WindowContext/Application.h"
#include "AssetManager/AssetManager.h"

namespace Rapture {

DynamicDiffuseGI::DynamicDiffuseGI() {

    
    
}

DynamicDiffuseGI::~DynamicDiffuseGI() {

}

void DynamicDiffuseGI::createPipelines() {

    auto& app = Application::getInstance();
    auto proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    ShaderImportConfig shaderIrradianceBlendConfig;
    shaderIrradianceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_RADIANCE");
    ShaderImportConfig shaderDistanceBlendConfig;
    shaderDistanceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_DISTANCE");


    auto [probeTraceShader, probeTraceShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "SPIRV/ddgi/ProbeTrace.cs.spirv");
    auto [probeIrradianceBlendShader, probeIrradianceBlendShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "SPIRV/ddgi/ProbeBlending.cs.spirv", shaderIrradianceBlendConfig);
    auto [probeDistanceBlendShader, probeDistanceBlendShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "SPIRV/ddgi/ProbeBlending.cs.spirv", shaderDistanceBlendConfig);



}







}