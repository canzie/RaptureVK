#pragma once

#include "RDCommon.h"
#include "Textures/Texture.h"

#include <memory>
#include <array>

namespace Rapture {

#define MAX_CASCADES 5

class RadianceCascades {

public:

    RadianceCascades();
    ~RadianceCascades();

    void build(const BuildParams& buildParams);


private:
    void buildTextures();

private:
    std::array<RadianceCascadeLevel, MAX_CASCADES> m_radianceCascades;
    std::array<std::shared_ptr<Texture>, MAX_CASCADES> m_cascadeTextures;

    };
}