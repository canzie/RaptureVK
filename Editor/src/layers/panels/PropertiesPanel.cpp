#include "PropertiesPanel.h"

PropertiesPanel::PropertiesPanel(Amethyst::DockingLayer *dockingLayer)
    : m_dockingLayer(dockingLayer)
{
    auto root = std::make_unique<Amethyst::PanelLayer>();
    m_root = root.get();
    m_root->name = "Properties";
    m_root->markDirty();

    m_placeholderText = m_root->add<Amethyst::TextLabel>();
    m_placeholderText->text = "Select an entity";
    m_placeholderText->textColor = Amethyst::Color4(0.5f, 0.5f, 0.5f, 1.0f);
    m_placeholderText->size = Amethyst::UDim2::fromScale(1.0f);
    m_placeholderText->textXAlignment = Amethyst::TextXAlignment::CENTER;
    m_placeholderText->textYAlignment = Amethyst::TextYAlignment::CENTER;
    m_placeholderText->backgroundTransparency = 1.0f;
    m_placeholderText->markDirty();

    m_dockingLayer->dock(std::move(root), glm::vec2(0.0f));
}

PropertiesPanel::~PropertiesPanel()
{
    if (m_dockingLayer && m_root) {
        m_dockingLayer->undock(m_root);
    }
}
