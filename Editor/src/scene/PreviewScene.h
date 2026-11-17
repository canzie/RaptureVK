#ifndef RAPTURE__PREVIEW_SCENE_H
#define RAPTURE__PREVIEW_SCENE_H

#include <rapture/core/ecs/entity_accessor.h>
#include <rapture/renderer/viewport/Viewport.h>

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Rapture {
class Mesh3D;
class Scene;
} // namespace Rapture

class EditorCamera;

/**
 * @brief A scene made to show one thing in, thrown away with whatever opened it.
 *
 * Nothing spawned into it is authored content, so it is never saved.
 */
class PreviewScene {
  public:
    /**
     * @brief Creates the scene, which has no viewport until one is asked for
     * @param name Name the scene and its viewport go by
     */
    explicit PreviewScene(std::string_view name);
    ~PreviewScene();

    PreviewScene(const PreviewScene &) = delete;
    PreviewScene &operator=(const PreviewScene &) = delete;

    /**
     * @brief Adds a grid floor, a sun and an atmosphere sky for the previewed thing to sit against
     */
    void addGroundAndSky();

    /**
     * @brief Creates the viewport this scene is drawn through
     * @param camera Camera to draw from, invalid to draw from an editor camera this scene creates
     * @return The viewport
     */
    Rapture::Viewport *createViewport(Rapture::ecs::EntityAccessor camera = {});

    /**
     * @brief Frames the camera on a box, so what it holds fills the view
     * @param min Corner of the world space box to frame
     * @param max Opposite corner of the world space box to frame
     */
    void focusOn(const glm::vec3 &min, const glm::vec3 &max);

    /**
     * @brief Moves meshes so the box they fill sits on the origin, then frames the camera on it
     * @param meshes The meshes to place, which keep their positions relative to one another
     * @return How far they moved, for anything else that has to move with them
     */
    glm::vec3 centreMeshOnOrigin(std::span<Rapture::Mesh3D *const> meshes);

    void onUpdate(float dt);

    Rapture::Scene *scene() const { return m_scene.get(); }
    Rapture::Viewport *viewport() const { return m_viewport.viewport; }

    /**
     * @brief The camera the viewport is drawn from, when the viewport was not handed one
     * @return The editor camera, or nullptr
     */
    EditorCamera *editorCamera() const { return m_editorCamera.get(); }

  private:
    std::string m_name;
    std::unique_ptr<Rapture::Scene> m_scene;
    std::unique_ptr<EditorCamera> m_editorCamera;
    Rapture::ViewportContext m_viewport;
};

#endif // RAPTURE__PREVIEW_SCENE_H
