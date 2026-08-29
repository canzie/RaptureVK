#ifndef RAPTURE__MESH3D_H
#define RAPTURE__MESH3D_H

#include "assets/asset_manager/AssetCommon.h"
#include "scene/EntityCommon.h"
#include "scene/instances/Node3D.h"

#include <vector>

namespace Rapture {

class AMesh;

/**
 * @brief Shared surface of everything drawn from a mesh and a material.
 */
class Mesh3D : public Node3D {
  public:
    ~Mesh3D() override;

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    AssetHandle mesh() const { return m_mesh; }
    virtual void setMesh(AssetHandle mesh) = 0;

    /**
     * @brief The materials this object draws its mesh's runs with
     * @return One material per run, in the same order the mesh slots them
     */
    const std::vector<AssetHandle> &materials() const { return m_materials; }

    /**
     * @brief The material this object draws one run with
     * @param slot The run to read
     * @return The material, or INVALID_ASSET_HANDLE if this object has no such run
     */
    AssetHandle material(uint32_t slot) const;

    /**
     * @brief Draws one run of this object's mesh with a material of its own
     * @param slot The run to set, which has to be one the mesh has
     * @param material The material to draw it with
     */
    void setMaterial(uint32_t slot, AssetHandle material);

    virtual bool isVisible() const = 0;
    virtual void setVisible(bool visible) = 0;

    virtual Mobility mobility() const = 0;
    virtual void setMobility(Mobility mobility) = 0;

    virtual glm::vec3 boundsMin() const = 0;
    virtual glm::vec3 boundsMax() const = 0;

    bool isRayTraced() const;

    /**
     * @brief Builds or drops the acceleration structure this mesh contributes to the scene TLAS
     * @param rayTraced Whether the mesh takes part in ray tracing
     */
    virtual void setRayTraced(bool rayTraced) = 0;

    void serialize(WriteNode node) const override;
    void deserialize(ReadNode node) override;

  protected:
    Mesh3D(Scene &scene, std::string_view name);

    /**
     * @brief Gives this object one material per run of a mesh, taking the mesh's own where it has none
     * @param mesh The mesh this object was just given
     */
    void adoptMaterialSlots(const AMesh &mesh);

  protected:
    AssetHandle m_mesh = INVALID_ASSET_HANDLE;

  private:
    std::vector<AssetHandle> m_materials;
};

} // namespace Rapture

#endif // RAPTURE__MESH3D_H
