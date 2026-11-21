#ifndef RAPTURE__SCENE_UNDO_RECORDS_H
#define RAPTURE__SCENE_UNDO_RECORDS_H

#include "undo/UndoHistory.h"

#include <rapture/scene/instances/Instance.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace Rapture {
class Scene;
class SceneObject;
} // namespace Rapture

class EntitySelection;

/**
 * @brief A change made to one scene object, which it refers to by id
 */
class SceneObjectUndoRecordBase : public UndoRecordBase {
  public:
    SceneObjectUndoRecordBase(Rapture::Scene &scene, Rapture::InstanceId objectId) : m_scene(scene), m_objectId(objectId) {}

    Rapture::InstanceId objectId() const { return m_objectId; }

  protected:
    Rapture::Scene &m_scene;
    Rapture::InstanceId m_objectId;
};

/**
 * @brief An object's own fields and components before and after a change, leaving its children out
 */
class SceneObjectChangeRecord : public SceneObjectUndoRecordBase {
  public:
    /**
     * @brief Takes down the object as it is before the change
     * @param scene The scene the object is in
     * @param object The object about to change
     */
    SceneObjectChangeRecord(Rapture::Scene &scene, const Rapture::SceneObject &object);

    void undo() override;
    void redo() override;
    void finish() override;
    bool isNoOp() const override { return m_before == m_after; }
    size_t byteSize() const override;

  private:
    /**
     * @brief Reads a state back into the object
     * @param state The object as it was written
     */
    void applyState(std::string_view state);

  private:
    std::string m_before;
    std::string m_after;
    bool m_finished = false;
};

/**
 * @brief The parent of an object and its place among its siblings, before and after a move
 */
class SceneObjectMoveRecord : public SceneObjectUndoRecordBase {
  public:
    /**
     * @brief Takes down where the object sits before the move
     * @param scene The scene the object is in
     * @param object The object about to move
     */
    SceneObjectMoveRecord(Rapture::Scene &scene, const Rapture::SceneObject &object);

    void undo() override { moveTo(m_before); }
    void redo() override { moveTo(m_after); }
    void finish() override;
    bool isNoOp() const override;
    size_t byteSize() const override { return sizeof(*this); }

  private:
    struct Place {
        Rapture::InstanceId parentId = Rapture::INVALID_INSTANCE_ID;
        size_t siblingIndex = 0;
    };

    static Place placeOfObject(const Rapture::SceneObject &object);

    void moveTo(const Place &place);

  private:
    Place m_before;
    Place m_after;
    bool m_finished = false;
};

/**
 * @brief Whether a subtree record is of an object that was added or one that was deleted
 */
enum SceneSubtreeChange {
    SCENE_SUBTREE_ADDED,
    SCENE_SUBTREE_DELETED
};

/**
 * @brief An object and everything below it, along with where it sits, that an edit added or deleted
 */
class SceneSubtreeRecord : public SceneObjectUndoRecordBase {
  public:
    /**
     * @brief Records an object that was just added, or one about to be deleted
     * @param scene The scene the object is in
     * @param selection The selection that lets go of the object when it is taken out again
     * @param object The object added or about to be deleted
     * @param change Which of the two happened
     */
    SceneSubtreeRecord(Rapture::Scene &scene, EntitySelection &selection, const Rapture::SceneObject &object,
                       SceneSubtreeChange change);

    void undo() override;
    void redo() override;
    void finish() override;
    bool isNoOp() const override { return m_subtree.empty(); }
    size_t byteSize() const override { return sizeof(*this) + m_subtree.capacity(); }

  private:
    void capture(const Rapture::SceneObject &object);
    void insert();
    void remove();

  private:
    EntitySelection &m_selection;
    SceneSubtreeChange m_change;
    Rapture::InstanceId m_parentId = Rapture::INVALID_INSTANCE_ID;
    size_t m_siblingIndex = 0;
    std::string m_subtree;
    bool m_finished = false;
};

#endif // RAPTURE__SCENE_UNDO_RECORDS_H
