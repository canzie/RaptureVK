#include "DynamicDiffuseGI.h"

#include "AssetManager/Asset.h"
#include "Components/Components.h"
#include "Materials/MaterialParameters.h"
#include "Renderer/GI/DDGI/DDGICommon.h"
#include "Scenes/Entities/Entity.h"
#include "Scenes/Scene.h"
#include "Textures/Texture.h"
#include "Textures/TextureCommon.h"
#include "WindowContext/Application.h"
#include "AssetManager/AssetManager.h"

#include <memory>
#include <vector>


namespace Rapture {

DynamicDiffuseGI::DynamicDiffuseGI() 
  : m_ProbeInfoBuffer(nullptr),
    m_Hysteresis(0.96f){

    auto& app = Application::getInstance();
    auto& vc = app.getVulkanContext();
    m_allocator = vc.getVmaAllocator();

    createPipelines();

    initProbeInfoBuffer();
    initTextures();
    
}

DynamicDiffuseGI::~DynamicDiffuseGI() {

}

void DynamicDiffuseGI::createPipelines() {

    auto& app = Application::getInstance();
    auto& proj = app.getProject();
    auto shaderDir = proj.getProjectShaderDirectory();

    ShaderImportConfig shaderIrradianceBlendConfig;
    shaderIrradianceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_RADIANCE");
    shaderIrradianceBlendConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";
    ShaderImportConfig shaderDistanceBlendConfig;
    shaderDistanceBlendConfig.compileInfo.macros.push_back("DDGI_BLEND_DISTANCE");
    shaderDistanceBlendConfig.compileInfo.includePath = shaderDir / "glsl/ddgi/";


    auto [probeTraceShader, probeTraceShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeTrace.cs.glsl", shaderDistanceBlendConfig);
    auto [probeIrradianceBlendShader, probeIrradianceBlendShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeBlending.cs.glsl", shaderIrradianceBlendConfig);
    auto [probeDistanceBlendShader, probeDistanceBlendShaderHandle] = AssetManager::importAsset<Shader>(shaderDir / "glsl/ddgi/ProbeBlending.cs.glsl", shaderDistanceBlendConfig);



}

void DynamicDiffuseGI::populateProbes(std::shared_ptr<Scene> scene){

  auto tlas = scene->getTLAS();

  auto& tlasInstances = tlas.getInstances();


  auto& reg = scene->getRegistry();
  auto materialView = reg.view<MaterialComponent>();

 
  std::vector<MeshInfo> meshInfos(tlas.getInstanceCount());

  int i = 0;

  for (auto inst : tlasInstances) {
    Entity ent = Entity(inst.instanceCustomIndex, scene.get());

    MeshInfo meshinfo = {};

    if (materialView.contains(ent)) {
      auto matComp = materialView.get<MaterialComponent>(ent);
      auto material = matComp.material;
      auto tex = std::get<std::shared_ptr<Texture>>(material->getParameter(ParameterID::ALBEDO_MAP));
   
      if (tex) {

        

      }
    }

    meshInfos[i] = meshinfo;

    i++;
  }

}

void DynamicDiffuseGI::initTextures(){

  TextureSpecification irradianceSpec;
  irradianceSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  irradianceSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  irradianceSpec.depth = m_ProbeVolume.gridDimensions.y;
  irradianceSpec.type = TextureType::TEXTURE2D_ARRAY;
  irradianceSpec.format = TextureFormat::R11G11B10F;
  irradianceSpec.filter = TextureFilter::Linear;
  irradianceSpec.storageImage = true;
  irradianceSpec.wrap = TextureWrap::ClampToEdge;

  TextureSpecification distanceSpec;
  distanceSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  distanceSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  distanceSpec.depth = m_ProbeVolume.gridDimensions.y;
  distanceSpec.type = TextureType::TEXTURE2D_ARRAY;
  distanceSpec.format = TextureFormat::RG16F;
  distanceSpec.filter = TextureFilter::Linear;
  distanceSpec.storageImage = true;
  distanceSpec.wrap = TextureWrap::ClampToEdge;

  TextureSpecification rayDataSpec;
  rayDataSpec.width = m_ProbeVolume.gridDimensions.x * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  rayDataSpec.height = m_ProbeVolume.gridDimensions.z * m_ProbeVolume.probeNumIrradianceInteriorTexels;
  rayDataSpec.depth = m_ProbeVolume.gridDimensions.y;
  rayDataSpec.type = TextureType::TEXTURE2D_ARRAY;
  rayDataSpec.format = TextureFormat::RGBA32F;
  rayDataSpec.filter = TextureFilter::Nearest;
  rayDataSpec.storageImage = true;
  rayDataSpec.wrap = TextureWrap::ClampToEdge;

  m_RadianceTexture = std::make_shared<Texture>(rayDataSpec);
  AssetVariant variant =  m_RadianceTexture;
  std::shared_ptr<AssetVariant> variantPtr = std::make_shared<AssetVariant>(variant);

  AssetManager::registerVirtualAsset(variantPtr, "[DDGI] Ray Data Texture", AssetType::Texture);

}

void DynamicDiffuseGI::initProbeInfoBuffer() {

        ProbeVolume probeVolume;

        probeVolume.origin = glm::vec3(-0.4f, 5.4f, -0.25f);
        
        probeVolume.rotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        probeVolume.probeRayRotation = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        
        probeVolume.spacing = glm::vec3(1.02f, 1.5f, 1.02f);
        probeVolume.gridDimensions = glm::uvec3(24, 8, 24);
        
        probeVolume.probeNumRays = 256;
        probeVolume.probeNumIrradianceInteriorTexels = 8;
        probeVolume.probeNumDistanceInteriorTexels = 16;

        probeVolume.probeHysteresis = 0.97f;
        probeVolume.probeMaxRayDistance = 10000.0f;
        probeVolume.probeNormalBias = 0.1f;
        probeVolume.probeViewBias = 0.1f;
        probeVolume.probeDistanceExponent = 10.0f;
        probeVolume.probeIrradianceEncodingGamma = 2.2f;

        probeVolume.probeBrightnessThreshold = 0.1f;

        probeVolume.probeMinFrontfaceDistance = 0.1f;
    
        probeVolume.probeRandomRayBackfaceThreshold = 0.1f;
        probeVolume.probeFixedRayBackfaceThreshold = 0.25f;

        m_ProbeVolume = probeVolume;

        m_ProbeInfoBuffer = std::make_shared<UniformBuffer>(sizeof(ProbeVolume), BufferUsage::STATIC, m_allocator, &probeVolume);

}

}

