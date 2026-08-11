#pragma once

/*
    Stores the state part of the ecs, mainly the data/instance of a system
*/

#include "ComponentsCommon.h"
#include "asset_manager/Asset.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "components/systems/BoundingBox.h"
#include "components/systems/Transforms.h"

#include "renderer/Frustum.h"

#include "asset_manager/AssetManager.h"
#include "buffers/StorageBuffer.h"
#include "buffers/UniformBuffer.h"
#include "cameras/PerspectiveCamera.h"
#include "materials/MaterialInstance.h"
#include "meshes/Mesh.h"

#include "asset_manager/AssetManager.h"
#include "components/ChangeChannels.h"
#include "ecs/entity_accessor.h"
#include "scenes/entities/EntityCommon.h"
#include "textures/Texture.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Rapture {

struct TagComponent {
    std::string tag;
};

// local is what the owning instance authored, world is local composed with every ancestor
struct TransformComponent {
    static constexpr ecs::ChangeMask CHANGE_CHANNELS = ecs::ChannelBit(CHANNEL_TRANSFORM_WORLD);

    glm::mat4 local{1.0f};
    glm::mat4 world{1.0f};
};

// Pure camera component - only contains camera-specific data
struct CameraComponent {
    static constexpr ecs::ChangeMask CHANGE_CHANNELS = ecs::ChannelBit(CHANNEL_CAMERA_PARAMS);

    PerspectiveCamera camera;
    Frustum frustum;

    float fov;
    float aspectRatio;
    float nearPlane;
    float farPlane;

    // whether the slot holds matrices from a previous update, so prevViewProj can carry them
    bool hasRenderData = false;

    CameraComponent(float fovy = 45.0f, float ar = 16.0f / 9.0f, float near_ = 0.1f, float far_ = 100.0f)
        : fov(fovy), aspectRatio(ar), nearPlane(near_), farPlane(far_)
    {
        camera = PerspectiveCamera(fovy, ar, near_, far_);
        frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
    }

    void updateProjectionMatrix(float fovy, float ar, float near_, float far_)
    {
        fov = fovy;
        aspectRatio = ar;
        nearPlane = near_;
        farPlane = far_;
        camera.updateProjectionMatrix(fovy, ar, near_, far_);
        frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
    }

    /**
     * @brief Rebuilds the view from where a transform sits in the world and the way it faces
     * @param transform The transform the camera is placed by
     */
    void updateViewMatrix(const TransformComponent &transform)
    {
        updateViewMatrix(transform::translation(transform.world), transform::forward(transform.world));
    }

    /**
     * @brief Rebuilds the view from where a transform sits in the world, looking along a given direction
     * @param transform The transform the camera is placed by
     * @param front The direction it looks along
     */
    void updateViewMatrix(const TransformComponent &transform, const glm::vec3 &front)
    {
        updateViewMatrix(transform::translation(transform.world), front);
    }

    /**
     * @brief Rebuilds the view from a position and direction directly
     * @param position Where the camera sits in the world
     * @param front The direction it looks along
     */
    void updateViewMatrix(const glm::vec3 &position, const glm::vec3 &front)
    {
        camera.updateViewMatrix(position, front);
        frustum.update(camera.getProjectionMatrix(), camera.getViewMatrix());
    }
};

struct MaterialComponent {
    static constexpr ecs::ChangeMask CHANGE_CHANNELS = ecs::ChannelBit(CHANNEL_MATERIAL_BINDING);

    AssetPtr<MaterialInstance> material;

    MaterialComponent() = default;

    MaterialComponent(AssetRef ref) : material(std::move(ref)) {}
};

struct MeshComponent {
    static constexpr ecs::ChangeMask CHANGE_CHANNELS = ecs::ChannelBit(CHANNEL_MESH_BINDING);

    AssetPtr<Mesh> mesh;
    bool isLoading = true;
    Mobility mobility = MOBILITY_STATIC;
    bool isEnabled = true;
    BoundingBox worldBoundingBox;

    MeshComponent() = default;

    MeshComponent(AssetRef ref, Mobility mob = MOBILITY_STATIC) : mesh(std::move(ref)), mobility(mob) { isLoading = false; }

    /**
     * @brief Replaces the mesh, invalidating the world bounding box the old one produced
     * @param ref Reference to the new mesh
     */
    void setMesh(AssetRef ref) { mesh = AssetPtr<Mesh>(std::move(ref)); }

    /**
     * @brief Recomputes the world bounding box from the mesh's bounds
     * @param transform The transform placing this mesh in the world
     */
    void updateWorldBoundingBox(const TransformComponent &transform)
    {
        if (!mesh) {
            return;
        }
        worldBoundingBox = BoundingBox(mesh->getBoundsMin(), mesh->getBoundsMax()).transform(transform.world);
    }
};

struct LightComponent {
    static constexpr ecs::ChangeMask CHANGE_CHANNELS = ecs::ChannelBit(CHANNEL_LIGHT_PARAMS);

    glm::vec3 color = glm::vec3(1.0f, 0.8f, 0.6f); // Light color (default: warm white?) #FFDDAA
    float intensity = 1.0f;                        // Light intensity multiplier

    bool isActive = true;
    Mobility mobility = MOBILITY_STATIC;
    bool castsShadow = false;

    void setColor(const glm::vec3 &c) { color = c; }

    void setIntensity(float i) { intensity = i; }

    void setActive(bool active) { isActive = active; }

    void setCastsShadow(bool casts) { castsShadow = casts; }

  protected:
    LightComponent() = default;
    LightComponent(const glm::vec3 &color, float intensity) : color(color), intensity(intensity) {}

};

struct DirectionalLightComponent : public LightComponent {
    bool atmosphereSunLight = false;

    DirectionalLightComponent() = default;
    DirectionalLightComponent(const glm::vec3 &color, float intensity) : LightComponent(color, intensity) {}
};

struct PointLightComponent : public LightComponent {
    float range = 10.0f;

    PointLightComponent() = default;
    PointLightComponent(const glm::vec3 &color, float intensity, float range) : LightComponent(color, intensity), range(range) {}
};

struct SpotLightComponent : public LightComponent {
    float range = 10.0f;
    float innerConeAngle = glm::radians(30.0f);
    float outerConeAngle = glm::radians(45.0f);

    SpotLightComponent() = default;
    SpotLightComponent(const glm::vec3 &color, float intensity, float range, float innerAngleDegrees, float outerAngleDegrees)
        : LightComponent(color, intensity), range(range), innerConeAngle(glm::radians(innerAngleDegrees)),
          outerConeAngle(glm::radians(outerAngleDegrees))
    {
    }
};

inline LightType Light_getLightType(const ecs::EntityAccessor &entity)
{
    if (entity.has<DirectionalLightComponent>()) {
        return LightType::DIRECTIONAL;
    }
    if (entity.has<SpotLightComponent>()) {
        return LightType::SPOT;
    }
    return LightType::POINT;
}

/**
 * @brief Mutable access to whichever concrete light an entity carries
 * @param entity The entity to write to
 * @param channels Channels the write announces, zero for bookkeeping that is not a change
 * @return The light, or nullptr if the entity carries none
 */
inline LightComponent *Light_tryWriteLight(const ecs::EntityAccessor &entity, ecs::ChangeMask channels)
{
    if (entity.has<DirectionalLightComponent>()) {
        return &*entity.write<DirectionalLightComponent>(channels);
    }
    if (entity.has<PointLightComponent>()) {
        return &*entity.write<PointLightComponent>(channels);
    }
    if (entity.has<SpotLightComponent>()) {
        return &*entity.write<SpotLightComponent>(channels);
    }
    return nullptr;
}

inline const LightComponent *Light_tryReadLight(const ecs::EntityAccessor &entity)
{
    if (const auto *directional = entity.tryRead<DirectionalLightComponent>()) {
        return directional;
    }
    if (const auto *point = entity.tryRead<PointLightComponent>()) {
        return point;
    }
    if (const auto *spot = entity.tryRead<SpotLightComponent>()) {
        return spot;
    }
    return nullptr;
}

// Marks an entity as participating in ray tracing
struct RayTracedComponent {};

struct ShadowComponent {
    static constexpr ecs::ChangeMask CHANGE_CHANNELS = ecs::ChannelBit(CHANNEL_SHADOW_SETTINGS);

    uint32_t resolution = 1024;
    bool isActive = true;
    Mobility mobility = MOBILITY_DYNAMIC;

    ShadowComponent() = default;
    explicit ShadowComponent(uint32_t resolution) : resolution(resolution) {}

};

struct CascadedShadowComponent {
    static constexpr ecs::ChangeMask CHANGE_CHANNELS = ecs::ChannelBit(CHANNEL_SHADOW_SETTINGS);

    uint32_t resolution = 2048;
    uint8_t numCascades = 4;
    float lambda = 0.8f;
    float shadowDistance = 80.0f;
    bool isActive = true;
    Mobility mobility = MOBILITY_DYNAMIC;

    CascadedShadowComponent() = default;
    CascadedShadowComponent(uint32_t resolution, uint8_t numCascades, float lambda)
        : resolution(resolution), numCascades(numCascades), lambda(lambda)
    {
    }

};

} // namespace Rapture
