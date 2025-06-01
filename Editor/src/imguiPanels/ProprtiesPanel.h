#pragma once

#include <imgui.h>

#include "Scenes/Entities/Entity.h"
#include <memory>

class PropertiesPanel {
public:
    PropertiesPanel();
    ~PropertiesPanel();

    void render();

    // Helper function to display a help marker with tooltip
    static void HelpMarker(const char* desc);

private:
    void renderMaterialComponent();
    void renderTransformComponent();

private:

    std::weak_ptr<Rapture::Entity> m_selectedEntity;

    size_t m_entitySelectedListenerId;
};
