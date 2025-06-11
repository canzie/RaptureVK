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

    auto [probeTraceShader, probeTraceShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "SPIRV/ddgi/ProbeTrace.cs.spirv");
    auto [probeBlendingShader, probeBlendingShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "SPIRV/ddgi/ProbeBlending.cs.spirv");



}







}