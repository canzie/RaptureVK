#ifndef RAPTURE__ADD_COMPONENT_WINDOW_H
#define RAPTURE__ADD_COMPONENT_WINDOW_H

#include <amethyst/Amethyst.h>
#include <components/popup.h>
#include <components/ui_scope.h>
#include <rapture/core/events/EventSignal.h>

#include <functional>
#include <string>
#include <string_view>

namespace Rapture {
class SceneObject;
struct TypeInfo;
} // namespace Rapture

class SearchField;

/**
 * @brief The window a scene component is picked from and attached to one scene object
 */
class AddComponentWindow {
  public:
    /**
     * @brief Builds the window, closed, as a child of a frame
     * @param parent The frame the window belongs to, which it is destroyed with
     */
    explicit AddComponentWindow(Amethyst::Frame &parent);

    AddComponentWindow(const AddComponentWindow &) = delete;
    AddComponentWindow &operator=(const AddComponentWindow &) = delete;

    /**
     * @brief Opens the window over the editor, listing what can be attached to an object
     * @param target The object a picked component is attached to
     */
    void open(Rapture::SceneObject &target);

    void close();

  public:
    /**
     * @brief Called after a component was attached to the target
     */
    std::function<void()> onComponentAdded;

  private:
    void buildTitleBar(Amethyst::UIScope &window);
    void buildBody(Amethyst::UIScope &window);

    /**
     * @brief Lists every registered component class whose name holds the search text
     * @param search Text a listed name has to hold, empty to list every one
     */
    void fillRows(std::string_view search);

    /**
     * @brief Adds one component class's row, and the details it expands into, to the list
     * @param rows Scope of the list
     * @param type The component class
     * @param order Place of the row in the list
     */
    void buildRow(Amethyst::UIScope &rows, const Rapture::TypeInfo &type, uint32_t order);

    /**
     * @brief Attaches a component of a class to the target and closes the window
     * @param className The registered class to attach
     */
    void addComponentToTarget(std::string_view className);

  private:
    Amethyst::Popup *m_window = nullptr;
    Amethyst::Frame *m_titleBar = nullptr;
    Amethyst::TextLabel *m_targetLabel = nullptr;
    SearchField *m_search = nullptr;
    Amethyst::Frame *m_rows = nullptr;
    Amethyst::EventConnection m_windowDestroyConn;

    Rapture::SceneObject *m_target = nullptr;
    Rapture::EventConnection m_targetDestroyConn;
    std::string m_loweredSearch;
};

#endif // RAPTURE__ADD_COMPONENT_WINDOW_H
