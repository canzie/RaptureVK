#include "EditedScene.h"

#include "EntitySelection.h"
#include "context/EditorContext.h"
#include "undo/SceneUndoRecords.h"

#include <rapture/core/serialization/SerialDocument.h>
#include <rapture/scene/Scene.h>

#include <memory>
#include <string>

static std::string s_writeSubtree(const Rapture::SceneObject &object)
{
    Rapture::SerialDocument document;
    object.serializeSubtree(document.root());
    return document.toText();
}

EditedScene::EditedScene(Rapture::Scene &scene, Rapture::SceneObject &contentRoot, EntitySelection &selection,
                         SceneObjectClipboard &clipboard, UndoHistoryLimits limits)
    : m_scene(scene), m_contentRoot(contentRoot), m_selection(selection), m_clipboard(clipboard), m_undoHistory(limits)
{
}

void EditedScene::rename(Rapture::SceneObject &object, std::string_view name)
{
    if (name.empty() || name == object.name()) {
        return;
    }

    recordChange(object);
    object.setName(name);
    m_scene.onHierarchyChanged.fire();
}

void EditedScene::destroy(Rapture::SceneObject &object)
{
    if (!isMovableContent(object)) {
        return;
    }

    // what this step already recorded below the object is taken down now, while it is still there
    for (const auto &record : m_undoHistory.openRecords()) {
        const auto *objectRecord = dynamic_cast<const SceneObjectUndoRecordBase *>(record.get());
        const Rapture::SceneObject *recorded = objectRecord != nullptr ? m_scene.findObjectById(objectRecord->objectId()) : nullptr;
        if (recorded != nullptr && recorded->isInSubtreeOf(object)) {
            record->finish();
        }
    }

    m_undoHistory.record(std::make_unique<SceneSubtreeRecord>(m_scene, m_selection, object, SCENE_SUBTREE_DELETED));

    const Rapture::SceneObject *selected = m_selection.has() ? m_scene.instanceFor(m_selection.entity().getEntity()) : nullptr;
    if (selected != nullptr && selected->isInSubtreeOf(object)) {
        m_selection.clear();
    }

    m_scene.destroyInstance(&object);
    m_scene.onHierarchyChanged.fire();
}

void EditedScene::destroyKeepingChildren(Rapture::SceneObject &object)
{
    Rapture::SceneObject *parent = object.parent();
    if (parent == nullptr || !isMovableContent(object)) {
        return;
    }

    while (!object.children().empty()) {
        Rapture::SceneObject *child = object.children().front().get();
        m_undoHistory.record(std::make_unique<SceneObjectMoveRecord>(m_scene, *child));
        if (!child->setParent(parent)) {
            break;
        }
    }

    destroy(object);
}

Rapture::SceneObject *EditedScene::duplicate(Rapture::SceneObject &object)
{
    if (!isMovableContent(object)) {
        return nullptr;
    }

    return spawnCopy(s_writeSubtree(object), *object.parent(), object.siblingIndex() + 1);
}

void EditedScene::copy(const Rapture::SceneObject &object)
{
    if (!isMovableContent(object)) {
        return;
    }

    m_clipboard.copiedSubtree = s_writeSubtree(object);
}

void EditedScene::cut(Rapture::SceneObject &object)
{
    if (!isMovableContent(object)) {
        return;
    }

    copy(object);
    destroy(object);
}

bool EditedScene::canPaste() const
{
    return !m_clipboard.copiedSubtree.empty();
}

Rapture::SceneObject *EditedScene::paste(Rapture::SceneObject *anchor)
{
    if (!canPaste()) {
        return nullptr;
    }

    if (anchor == nullptr || !isMovableContent(*anchor)) {
        return spawnCopy(m_clipboard.copiedSubtree, m_contentRoot, m_contentRoot.children().size());
    }

    return spawnCopy(m_clipboard.copiedSubtree, *anchor->parent(), anchor->siblingIndex() + 1);
}

void EditedScene::recordChange(const Rapture::SceneObject &object)
{
    for (const auto &record : m_undoHistory.openRecords()) {
        const auto *change = dynamic_cast<const SceneObjectChangeRecord *>(record.get());
        if (change != nullptr && change->objectId() == object.id()) {
            return;
        }
    }

    m_undoHistory.record(std::make_unique<SceneObjectChangeRecord>(m_scene, object));
}

bool EditedScene::isMovableContent(const Rapture::SceneObject &object) const
{
    return &object != &m_contentRoot && !object.isInternal() && object.isInSubtreeOf(m_contentRoot);
}

void EditedScene::onObjectAdded(Rapture::SceneObject &added)
{
    m_undoHistory.record(std::make_unique<SceneSubtreeRecord>(m_scene, m_selection, added, SCENE_SUBTREE_ADDED));
    m_scene.onHierarchyChanged.fire();
    m_selection.select(added.accessor());
}

Rapture::SceneObject *EditedScene::spawnCopy(std::string_view subtree, Rapture::SceneObject &parent, size_t siblingIndex)
{
    Rapture::SerialDocument document = Rapture::SerialDocument::parse(subtree);
    Rapture::SceneObject *copy = Rapture::SceneObject::spawnSubtree(parent, document.rootView());
    if (copy == nullptr) {
        return nullptr;
    }

    copy->setSiblingIndex(siblingIndex);
    onObjectAdded(*copy);
    return copy;
}
