#ifndef RAPTURE__STATIC_MESH_WORKSPACE_H
#define RAPTURE__STATIC_MESH_WORKSPACE_H

#include "PreviewWorkspace.h"
#include "inspector/AssetEditors.h"

#include <glm/glm.hpp>

namespace Rapture {
class StaticMesh3D;
} // namespace Rapture

/**
 * @brief The workspace one open static mesh asset is inspected in
 */
class StaticMeshWorkspace : public PreviewWorkspace {
  public:
    std::string_view typeName() const override { return "StaticMeshWorkspace"; }
    StaticMeshWorkspace(const EditorContext &context, Rapture::AssetHandle handle);
    ~StaticMeshWorkspace() override;

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override;

  private:
    /**
     * @brief Shows the bake transform on the preview mesh, keeping it centred on the origin
     */
    void previewBakeTransform();

    /**
     * @brief Writes the open mesh back to its asset file
     */
    void save();

    /**
     * @brief Imports the open mesh again from the file it came from
     */
    void reimport();

  private:
    Rapture::StaticMesh3D *m_mesh = nullptr;
    glm::vec3 m_centringOffset = glm::vec3(0.0f);
    glm::vec3 m_centredBoundsMin = glm::vec3(0.0f);
    glm::vec3 m_centredBoundsMax = glm::vec3(0.0f);
    MeshBakeTransform m_bakeTransform;
};

#endif // RAPTURE__STATIC_MESH_WORKSPACE_H
