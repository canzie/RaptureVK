#ifndef RAPTURE__EDITED_SCENE_H
#define RAPTURE__EDITED_SCENE_H

#include "undo/UndoHistory.h"

#include <rapture/scene/instances/SceneObject.h>

#include <concepts>
#include <cstddef>
#include <string_view>

namespace Rapture {
class Scene;
} // namespace Rapture

class EntitySelection;
struct SceneObjectClipboard;

/**
 * @brief A scene being edited in a workspace, with its undo history
 */
class EditedScene {
  public:
    /**
     * @brief Starts editing a scene
     * @param scene The scene to edit
     * @param contentRoot The object everything authored sits under
     * @param selection The selection of the workspace the scene is edited in
     * @param clipboard The clipboard copies go to and pastes come from
     * @param limits How much the undo history keeps
     */
    EditedScene(Rapture::Scene &scene, Rapture::SceneObject &contentRoot, EntitySelection &selection, SceneObjectClipboard &clipboard,
                UndoHistoryLimits limits);

    EditedScene(const EditedScene &) = delete;
    EditedScene &operator=(const EditedScene &) = delete;

    Rapture::Scene &scene() const { return m_scene; }
    Rapture::SceneObject &contentRoot() const { return m_contentRoot; }
    UndoHistory &undoHistory() { return m_undoHistory; }

    /**
     * @brief Adds a new object under a parent and selects it
     * @tparam T The class of the object
     * @param parent The object it is added under
     * @param name The name it gets
     * @return The object, nullptr if it could not be added
     */
    template <std::derived_from<Rapture::SceneObject> T>
    T *add(Rapture::SceneObject &parent, std::string_view name)
    {
        T *added = parent.add<T>(name);
        if (added != nullptr) {
            onObjectAdded(*added);
        }
        return added;
    }

    /**
     * @brief Renames an object
     * @param object The object to rename
     * @param name Its new name, ignored when empty
     */
    void rename(Rapture::SceneObject &object, std::string_view name);

    /**
     * @brief Deletes an object and everything below it
     * @param object The object to delete
     */
    void destroy(Rapture::SceneObject &object);

    /**
     * @brief Deletes an object after moving its children up to its parent
     * @param object The object to delete
     */
    void destroyKeepingChildren(Rapture::SceneObject &object);

    /**
     * @brief Copies an object in right after itself and selects the copy
     * @param object The object to duplicate
     * @return The copy, nullptr if it could not be made
     */
    Rapture::SceneObject *duplicate(Rapture::SceneObject &object);

    /**
     * @brief Puts an object on the clipboard
     * @param object The object to copy
     */
    void copy(const Rapture::SceneObject &object);

    /**
     * @brief Puts an object on the clipboard and deletes it
     * @param object The object to cut
     */
    void cut(Rapture::SceneObject &object);

    /**
     * @brief Whether the clipboard holds an object to paste
     * @return True if something was copied
     */
    bool canPaste() const;

    /**
     * @brief Pastes the clipboard right after an object and selects what was pasted
     * @param anchor The object the paste goes after, nullptr to paste at the end of the content root
     * @return The pasted object, nullptr if nothing could be pasted
     */
    Rapture::SceneObject *paste(Rapture::SceneObject *anchor);

    /**
     * @brief Records an object before it is changed in place, so the change can be undone
     * @param object The object about to change
     */
    void recordChange(const Rapture::SceneObject &object);

  private:
    /**
     * @brief Whether an object can be deleted, copied or moved
     * @param object The object to test
     * @return True for anything below the content root that is not internal
     */
    bool isMovableContent(const Rapture::SceneObject &object) const;

    /**
     * @brief Records a newly added object and selects it
     * @param added The object that was added
     */
    void onObjectAdded(Rapture::SceneObject &added);

    /**
     * @brief Adds a written subtree under a parent as new objects
     * @param subtree The subtree as it was written
     * @param parent The object it is added under
     * @param siblingIndex Where among the parent's children it goes
     * @return The root of what was read, nullptr if it could not be read
     */
    Rapture::SceneObject *spawnCopy(std::string_view subtree, Rapture::SceneObject &parent, size_t siblingIndex);

  private:
    Rapture::Scene &m_scene;
    Rapture::SceneObject &m_contentRoot;
    EntitySelection &m_selection;
    SceneObjectClipboard &m_clipboard;
    UndoHistory m_undoHistory;
};

#endif // RAPTURE__EDITED_SCENE_H
