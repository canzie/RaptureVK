#include "SceneUndoRecords.h"

#include "EntitySelection.h"

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/core/utils/Log.h>
#include <rapture/scene/Scene.h>
#include <rapture/scene/SceneLoadContext.h>
#include <rapture/scene/instances/SceneObject.h>

static Rapture::SceneObject *s_findObjectOrRoot(const Rapture::Scene &scene, Rapture::InstanceId id)
{
    Rapture::SceneObject *root = scene.root();
    if (root != nullptr && root->id() == id) {
        return root;
    }
    return scene.findObjectById(id);
}

static std::string s_writeObjectState(const Rapture::SceneObject &object)
{
    Rapture::SerialDocument document;
    object.serialize(document.root());
    return document.toText();
}

static std::string s_writeSubtree(const Rapture::SceneObject &object)
{
    Rapture::SerialDocument document;
    object.serializeSubtree(document.root());
    return document.toText();
}

static void s_readObjectStateInto(Rapture::Scene &scene, Rapture::SceneObject &object, std::string_view state)
{
    Rapture::SerialDocument document = Rapture::SerialDocument::parse(state);
    Rapture::ReadNode node = document.rootView();
    Rapture::SceneObject::DocumentHeader header = Rapture::SceneObject::readHeader(node);

    Rapture::SceneLoadContext context(scene, false);
    object.deserialize(node);
    context.addInstance(header.id, &object);

    if (!object.replaceComponents(header, context)) {
        RP_ERROR("the components of '{}' could not all be read back", object.name());
    }

    context.finish();
}

static void s_releaseSelectionInSubtree(Rapture::Scene &scene, EntitySelection &selection, const Rapture::SceneObject &root)
{
    if (!selection.has()) {
        return;
    }

    const Rapture::SceneObject *selected = scene.instanceFor(selection.entity().getEntity());
    if (selected != nullptr && selected->isInSubtreeOf(root)) {
        selection.clear();
    }
}

SceneObjectChangeRecord::SceneObjectChangeRecord(Rapture::Scene &scene, const Rapture::SceneObject &object)
    : SceneObjectUndoRecordBase(scene, object.id()), m_before(s_writeObjectState(object))
{
}

void SceneObjectChangeRecord::undo()
{
    applyState(m_before);
}

void SceneObjectChangeRecord::redo()
{
    applyState(m_after);
}

void SceneObjectChangeRecord::finish()
{
    if (m_finished) {
        return;
    }
    m_finished = true;

    const Rapture::SceneObject *object = m_scene.findObjectById(m_objectId);
    m_after = object != nullptr ? s_writeObjectState(*object) : m_before;
}

size_t SceneObjectChangeRecord::byteSize() const
{
    return sizeof(*this) + m_before.capacity() + m_after.capacity();
}

void SceneObjectChangeRecord::applyState(std::string_view state)
{
    Rapture::SceneObject *object = m_scene.findObjectById(m_objectId);
    if (object == nullptr) {
        RP_ERROR("the object a change was made to is no longer in the scene");
        return;
    }

    s_readObjectStateInto(m_scene, *object, state);

    // a name or a component is shown in the tree, so it is redrawn too
    m_scene.onHierarchyChanged.fire();
}

SceneObjectMoveRecord::SceneObjectMoveRecord(Rapture::Scene &scene, const Rapture::SceneObject &object)
    : SceneObjectUndoRecordBase(scene, object.id()), m_before(placeOfObject(object)), m_after(m_before)
{
}

void SceneObjectMoveRecord::finish()
{
    if (m_finished) {
        return;
    }
    m_finished = true;

    if (const Rapture::SceneObject *object = m_scene.findObjectById(m_objectId)) {
        m_after = placeOfObject(*object);
    }
}

bool SceneObjectMoveRecord::isNoOp() const
{
    return m_before.parentId == m_after.parentId && m_before.siblingIndex == m_after.siblingIndex;
}

SceneObjectMoveRecord::Place SceneObjectMoveRecord::placeOfObject(const Rapture::SceneObject &object)
{
    return {.parentId = object.parent() != nullptr ? object.parent()->id() : Rapture::INVALID_INSTANCE_ID,
            .siblingIndex = object.siblingIndex()};
}

void SceneObjectMoveRecord::moveTo(const Place &place)
{
    Rapture::SceneObject *object = m_scene.findObjectById(m_objectId);
    Rapture::SceneObject *parent = s_findObjectOrRoot(m_scene, place.parentId);
    if (object == nullptr || parent == nullptr || !object->setParent(parent)) {
        RP_ERROR("an object could not be moved back to where it was");
        return;
    }

    object->setSiblingIndex(place.siblingIndex);
    m_scene.onHierarchyChanged.fire();
}

SceneSubtreeRecord::SceneSubtreeRecord(Rapture::Scene &scene, EntitySelection &selection, const Rapture::SceneObject &object,
                                       SceneSubtreeChange change)
    : SceneObjectUndoRecordBase(scene, object.id()), m_selection(selection), m_change(change)
{
    // an added object is taken down once the step closes, so whatever else the step did to it is in it
    if (m_change == SCENE_SUBTREE_DELETED) {
        capture(object);
    }
}

void SceneSubtreeRecord::undo()
{
    if (m_change == SCENE_SUBTREE_ADDED) {
        remove();
    } else {
        insert();
    }
}

void SceneSubtreeRecord::redo()
{
    if (m_change == SCENE_SUBTREE_ADDED) {
        insert();
    } else {
        remove();
    }
}

void SceneSubtreeRecord::finish()
{
    if (m_finished) {
        return;
    }
    m_finished = true;

    if (m_change != SCENE_SUBTREE_ADDED) {
        return;
    }

    if (const Rapture::SceneObject *object = m_scene.findObjectById(m_objectId)) {
        capture(*object);
    }
}

void SceneSubtreeRecord::capture(const Rapture::SceneObject &object)
{
    m_parentId = object.parent() != nullptr ? object.parent()->id() : Rapture::INVALID_INSTANCE_ID;
    m_siblingIndex = object.siblingIndex();
    m_subtree = s_writeSubtree(object);
}

void SceneSubtreeRecord::insert()
{
    Rapture::SceneObject *parent = s_findObjectOrRoot(m_scene, m_parentId);
    if (parent == nullptr) {
        RP_ERROR("the parent an object was deleted from is no longer in the scene");
        return;
    }

    Rapture::SerialDocument document = Rapture::SerialDocument::parse(m_subtree);

    // brought back as the very objects that were taken out, so the steps around this one still find them
    Rapture::SceneLoadContext context(m_scene, false);
    Rapture::SceneObject *object = Rapture::SceneObject::loadSubtree(*parent, document.rootView(), context);
    if (object == nullptr) {
        RP_ERROR("a deleted object could not be read back into the scene");
        return;
    }

    object->setSiblingIndex(m_siblingIndex);
    context.finish();
    m_scene.onHierarchyChanged.fire();
}

void SceneSubtreeRecord::remove()
{
    Rapture::SceneObject *object = m_scene.findObjectById(m_objectId);
    if (object == nullptr) {
        return;
    }

    s_releaseSelectionInSubtree(m_scene, m_selection, *object);
    m_scene.destroyInstance(object);
    m_scene.onHierarchyChanged.fire();
}
