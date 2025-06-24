#pragma once

#include "ObjectDataBase.h"

namespace Rapture {

// Forward declarations
struct LightComponent;
struct TransformComponent;
struct ShadowComponent;
struct CascadedShadowComponent;

class ShadowDataBuffer : public ObjectDataBuffer {
public:
    ShadowDataBuffer();
    
    // Update from shadow component (regular shadow map)
    void update(const LightComponent& light, const ShadowComponent& shadow, uint32_t entityID);
    
    // Update from cascaded shadow component  
    void update(const LightComponent& light, const CascadedShadowComponent& shadow, uint32_t entityID);
    
    // Override the pure virtual update (can be empty since we use parameterized versions)
    //void update() override {}
};

} 