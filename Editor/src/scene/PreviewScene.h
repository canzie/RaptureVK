#ifndef RAPTURE__PREVIEW_SCENE_H
#define RAPTURE__PREVIEW_SCENE_H

#include <core/ecs/entity_accessor.h>
#include <renderer/viewport/Viewport.h>

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Rapture {
class Mesh3D;
class Scene;
} // namespace Rapture

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
     * @param camera Camera to draw from, invalid to let the viewport bring its own and a controller for it
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

  private:
    std::string m_name;
    std::unique_ptr<Rapture::Scene> m_scene;
    Rapture::ViewportContext m_viewport;
};

#endif // RAPTURE__PREVIEW_SCENE_H
