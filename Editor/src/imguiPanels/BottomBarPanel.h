#ifndef RAPTURE__BOTTOM_BAR_PANEL_H
#define RAPTURE__BOTTOM_BAR_PANEL_H

#include <functional>
#include <imgui.h>
#include <string>
#include <vector>

/**
 * @brief Bottom bar panel system with tabbed interface supporting hovering and locked modes
 */
class BottomBarPanel {
  public:
    enum class PanelMode {
        HOVERING, ///< Overlay mode (default) - panel floats above other UI
        LOCKED    ///< Docked mode - panel participates in docking system
    };

    enum class AutoHideBehavior {
        NONE,            ///< No auto-hide
        ON_MOUSE_LEAVE,  ///< Hide when mouse leaves panel area
        ON_CLICK_OUTSIDE ///< Hide when clicking outside panel (default)
    };

    struct RegisteredPanel {
        std::string id;
        std::string tabLabel;
        std::function<void()> renderCallback; ///< Callback to render panel content
        PanelMode mode;
        bool isOpen;
        float defaultHeight;
        float currentHeight;

        RegisteredPanel(const std::string &panelId, const std::string &label, std::function<void()> callback, float height = 300.0f)
            : id(panelId), tabLabel(label), renderCallback(callback), mode(PanelMode::HOVERING), isOpen(false),
              defaultHeight(height), currentHeight(height)
        {
        }
    };

    BottomBarPanel();
    ~BottomBarPanel();

    void render();

    void registerPanel(const std::string &id, const std::string &tabLabel, std::function<void()> renderCallback,
                       float defaultHeight = 300.0f);
    void unregisterPanel(const std::string &id);
    void openPanel(const std::string &id);
    void closePanel(const std::string &id);
    void togglePanelMode(const std::string &id);
    void setAutoHideBehavior(AutoHideBehavior behavior);
    void setTabBarHeight(float height);

  private:
    void renderTabBar();
    void renderActivePanel();
    void handleTabInteraction(RegisteredPanel &panel);
    void handleAutoHide();
    bool isMouseOverPanel() const;
    bool wasClickedOutside() const;

    std::vector<RegisteredPanel> m_registeredPanels;
    RegisteredPanel *m_activePanel = nullptr;

    AutoHideBehavior m_autoHideBehavior = AutoHideBehavior::ON_CLICK_OUTSIDE;
    float m_tabBarHeight = 30.0f;

    std::string m_lastClickedTab;
    float m_lastClickTime = 0.0f;
    static constexpr float DOUBLE_CLICK_TIME = 0.3f;

    ImVec2 m_panelMin;
    ImVec2 m_panelMax;
    ImVec2 m_tabBarMin;
    ImVec2 m_tabBarMax;
};

#endif // RAPTURE__BOTTOM_BAR_PANEL_H
