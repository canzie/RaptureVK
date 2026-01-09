#include "BottomBarPanel.h"
#include "imguiPanels/IconsMaterialDesign.h"
#include "imguiPanels/themes/imguiPanelStyle.h"
#include <algorithm>

BottomBarPanel::BottomBarPanel() {}

BottomBarPanel::~BottomBarPanel() {}

void BottomBarPanel::render()
{
    renderTabBar();
    renderActivePanel();
}

void BottomBarPanel::registerPanel(const std::string &id, const std::string &tabLabel, std::function<void()> renderCallback,
                                   float defaultHeight)
{
    m_registeredPanels.emplace_back(id, tabLabel, renderCallback, defaultHeight);
}

void BottomBarPanel::unregisterPanel(const std::string &id)
{
    auto it = std::find_if(m_registeredPanels.begin(), m_registeredPanels.end(),
                           [&id](const RegisteredPanel &panel) { return panel.id == id; });
    if (it != m_registeredPanels.end()) {
        if (m_activePanel == &(*it)) {
            m_activePanel = nullptr;
        }
        m_registeredPanels.erase(it);
    }
}

void BottomBarPanel::openPanel(const std::string &id)
{
    for (auto &panel : m_registeredPanels) {
        if (panel.id == id) {
            // Close currently active panel if different
            if (m_activePanel && m_activePanel->id != id) {
                m_activePanel->isOpen = false;
            }
            m_activePanel = &panel;
            panel.isOpen = true;
            return;
        }
    }
}

void BottomBarPanel::closePanel(const std::string &id)
{
    for (auto &panel : m_registeredPanels) {
        if (panel.id == id) {
            panel.isOpen = false;
            if (m_activePanel == &panel) {
                m_activePanel = nullptr;
            }
            return;
        }
    }
}

void BottomBarPanel::togglePanelMode(const std::string &id)
{
    for (auto &panel : m_registeredPanels) {
        if (panel.id == id) {
            panel.mode = (panel.mode == PanelMode::HOVERING) ? PanelMode::LOCKED : PanelMode::HOVERING;
            return;
        }
    }
}

void BottomBarPanel::setAutoHideBehavior(AutoHideBehavior behavior)
{
    m_autoHideBehavior = behavior;
}

void BottomBarPanel::setTabBarHeight(float height)
{
    m_tabBarHeight = height;
}

void BottomBarPanel::renderTabBar()
{
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImVec2 tabBarPos(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - m_tabBarHeight);
    ImVec2 tabBarSize(viewport->WorkSize.x, m_tabBarHeight);

    ImGui::SetNextWindowPos(tabBarPos);
    ImGui::SetNextWindowSize(tabBarSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::Begin("##BottomBarTabs", nullptr, flags);

    m_tabBarMin = ImGui::GetWindowPos();
    m_tabBarMax = ImVec2(m_tabBarMin.x + ImGui::GetWindowSize().x, m_tabBarMin.y + ImGui::GetWindowSize().y);

    for (auto &panel : m_registeredPanels) {
        handleTabInteraction(panel);
        if (&panel != &m_registeredPanels.back()) {
            ImGui::SameLine();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void BottomBarPanel::renderActivePanel()
{
    if (!m_activePanel || !m_activePanel->isOpen) {
        return;
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();

    ImVec2 panelPos(viewport->WorkPos.x,
                    viewport->WorkPos.y + viewport->WorkSize.y - m_tabBarHeight - m_activePanel->currentHeight);
    ImVec2 panelSize(viewport->WorkSize.x, m_activePanel->currentHeight);

    ImGui::SetNextWindowPos(panelPos);
    if (m_activePanel->mode == PanelMode::HOVERING) {
        ImGui::SetNextWindowSize(panelSize);
    } else {
        ImGui::SetNextWindowSize(panelSize, ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    if (m_activePanel->mode == PanelMode::HOVERING) {
        flags |= ImGuiWindowFlags_NoMove;
    } else {
        flags |= ImGuiWindowFlags_NoDocking;
    }

    std::string windowId = "##BottomBar_" + m_activePanel->id;
    ImGui::Begin(windowId.c_str(), &m_activePanel->isOpen, flags);

    m_panelMin = ImGui::GetWindowPos();
    m_panelMax = ImVec2(m_panelMin.x + ImGui::GetWindowSize().x, m_panelMin.y + ImGui::GetWindowSize().y);

    m_activePanel->currentHeight = ImGui::GetWindowSize().y;

    ImGui::Text("%s", m_activePanel->tabLabel.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
    if (ImGui::Button(ICON_MD_CLOSE)) {
        m_activePanel->isOpen = false;
    }
    ImGui::Separator();

    m_activePanel->renderCallback();

    ImGui::End();

    if (m_activePanel->mode == PanelMode::HOVERING) {
        handleAutoHide();
    }
}

void BottomBarPanel::handleTabInteraction(RegisteredPanel &panel)
{
    bool isActive = (m_activePanel == &panel && panel.isOpen);

    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColorPalette::ACCENT_PRIMARY);
    }

    if (ImGui::Button(panel.tabLabel.c_str())) {
        float currentTime = ImGui::GetTime();
        bool isDoubleClick = (m_lastClickedTab == panel.id && (currentTime - m_lastClickTime) < DOUBLE_CLICK_TIME);

        if (isDoubleClick) {
            togglePanelMode(panel.id);
            m_lastClickedTab.clear();
        } else {
            if (isActive) {
                closePanel(panel.id);
            } else {
                openPanel(panel.id);
            }
            m_lastClickedTab = panel.id;
            m_lastClickTime = currentTime;
        }
    }

    if (isActive) {
        ImGui::PopStyleColor();
    }
}

void BottomBarPanel::handleAutoHide()
{
    if (m_autoHideBehavior == AutoHideBehavior::NONE) {
        return;
    }

    if (m_autoHideBehavior == AutoHideBehavior::ON_MOUSE_LEAVE) {
        if (!isMouseOverPanel()) {
            if (m_activePanel) {
                m_activePanel->isOpen = false;
            }
        }
    } else if (m_autoHideBehavior == AutoHideBehavior::ON_CLICK_OUTSIDE) {
        if (wasClickedOutside()) {
            if (m_activePanel) {
                m_activePanel->isOpen = false;
            }
        }
    }
}

bool BottomBarPanel::isMouseOverPanel() const
{
    ImVec2 mousePos = ImGui::GetMousePos();

    bool overTabBar =
        (mousePos.x >= m_tabBarMin.x && mousePos.x <= m_tabBarMax.x && mousePos.y >= m_tabBarMin.y && mousePos.y <= m_tabBarMax.y);
    bool overPanel =
        (mousePos.x >= m_panelMin.x && mousePos.x <= m_panelMax.x && mousePos.y >= m_panelMin.y && mousePos.y <= m_panelMax.y);

    return overTabBar || overPanel;
}

bool BottomBarPanel::wasClickedOutside() const
{
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return false;
    }

    return !isMouseOverPanel();
}
