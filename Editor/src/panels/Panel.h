#ifndef RAPTURE__PANEL_H
#define RAPTURE__PANEL_H

#include "context/EditorContext.h"

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include <memory>
#include <string>
#include <string_view>

class EditorConfig;

class Panel {
  public:
    /**
     * @brief Creates a panel, which builds nothing until it is attached
     * @param name The panel's name, shown on its tab
     * @param context The workspace the panel is built against
     */
    Panel(std::string name, const WorkspaceContext &context);
    virtual ~Panel();

    Panel(const Panel &) = delete;
    Panel &operator=(const Panel &) = delete;

    /**
     * @brief Builds this panel and adds it to a bar as a tab
     * @param tabBar The bar the tab is added to
     */
    void attach(Amethyst::TabBar *tabBar);

    /**
     * @brief Builds this panel into a parent that is not a tab bar, filling it
     * @param parent The instance the panel's frame is added to
     */
    void attachTo(Amethyst::Instance &parent);

    /**
     * @brief Moves this panel's tab to another bar, leaving what it holds built
     * @param tabBar The bar the tab moves to
     */
    void moveTo(Amethyst::TabBar *tabBar);

    virtual void onUpdate(float dt) { (void)dt; }

    /**
     * @brief Lays this panel's contents out by the sizes the editor config currently holds.
     *
     * Called as the panel is built and again whenever those sizes change.
     */
    virtual void resolveLayout() {}

    /**
     * @brief Points this panel at a different workspace, for a panel that outlives the one it was built for
     * @param context The workspace the panel now belongs to
     */
    virtual void setContext(const WorkspaceContext &context);

    /**
     * @brief Tells this panel whether its tab is the one in front
     * @param shown True while it is the selected tab of its bar
     */
    void setShown(bool shown);

    bool isShown() const { return m_shown; }
    Amethyst::Frame *root() const { return m_root; }
    std::string_view name() const { return m_name; }

  protected:
    /**
     * @brief Fills this panel's frame, called once as it is attached
     * @param scope Scope of the frame this panel fills
     */
    virtual void build(Amethyst::UIScope &scope) = 0;

    /**
     * @brief Runs as this panel's tab becomes the one in front
     */
    virtual void onShown() {}

    /**
     * @brief Runs as this panel's tab stops being the one in front
     */
    virtual void onHidden() {}

    /**
     * @brief Runs once this panel's frame is gone, taking everything built into it
     */
    virtual void onRootDestroyed() {}

    const EditorConfig &config() const;

  protected:
    std::string m_name;
    std::string_view m_icon;
    WorkspaceContext m_context;

    Amethyst::Frame *m_root = nullptr;
    Amethyst::EventConnection m_rootDestroyConn;
    EntitySelection *m_selection = nullptr;

  private:
    /**
     * @brief Creates this panel's frame and fills it
     * @return The frame, not yet added to anything
     */
    std::unique_ptr<Amethyst::Frame> buildRoot();

  private:
    bool m_shown = false;
};

#endif // RAPTURE__PANEL_H
