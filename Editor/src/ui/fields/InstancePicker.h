#ifndef RAPTURE__INSTANCE_PICKER_H
#define RAPTURE__INSTANCE_PICKER_H

#include "core/events/EventSignal.h"

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include <functional>

class EntitySelection;

namespace Rapture {
class Scene;
class SceneObject;
class TypeInfo;
} // namespace Rapture

/**
 * @brief Editor scene object field: a bar naming the object it holds, which takes the next object clicked.
 */
class InstancePicker {
  public:
    /**
     * @brief Builds the field
     * @param parent Scope the field is built into
     * @param type The class this field accepts
     */
    InstancePicker(Amethyst::UIScope &parent, const Rapture::TypeInfo &type);
    ~InstancePicker();

    /**
     * @brief Points this field at what it picks out of
     * @param selection Where the object clicked is taken from
     * @param scene The scene the picked object lives in
     */
    void setSubject(EntitySelection *selection, Rapture::Scene *scene);

    InstancePicker(const InstancePicker &) = delete;
    InstancePicker &operator=(const InstancePicker &) = delete;

    /**
     * @brief Shows an object without firing onInstanceSelected
     * @param instance The object to show, or nullptr to show an empty field
     */
    void setInstance(Rapture::SceneObject *instance);
    Rapture::SceneObject *getInstance() const { return m_selected; }

    /**
     * @brief Waits for an object to be clicked, taking it as this field's
     */
    void armPick();

    /**
     * @brief Stops waiting for an object to be clicked
     */
    void cancelPick();

    bool isPicking() const { return m_isPicking; }

    Amethyst::Frame *getRoot() const { return m_root; }

  public:
    std::function<void(Rapture::SceneObject *)> onInstanceSelected;

  private:
    void buildFace(Amethyst::UIScope &parent);
    void applySelection();
    void takeInstance(Rapture::SceneObject *instance);

  private:
    const Rapture::TypeInfo *m_type = nullptr;
    EntitySelection *m_selection = nullptr;
    Rapture::Scene *m_scene = nullptr;

    Rapture::SceneObject *m_selected = nullptr;
    bool m_isPicking = false;

    Amethyst::Frame *m_root = nullptr;
    Amethyst::TextLabel *m_label = nullptr;
    Amethyst::ImageLabel *m_icon = nullptr;

    Rapture::EventConnection m_selectedDestroyed;
};

#endif // RAPTURE__INSTANCE_PICKER_H
