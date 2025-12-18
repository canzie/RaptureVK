#include "ProceduralTextures.h"

namespace Rapture {

ProceduralTexture::ProceduralTexture(const std::string &shaderPath, const std::vector<PushConstantInput> &pushConstantInputs)
{
    (void)shaderPath;
    (void)pushConstantInputs;
}

ProceduralTexture::~ProceduralTexture() {}

} // namespace Rapture