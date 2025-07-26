#pragma once

#include <imgui.h>


class SettingsPanel {
    public:
        SettingsPanel();
        ~SettingsPanel();

        void render();

        void renderRendererSettings();
        void renderDDGISettings();
        void renderRadianceCascadeSettings();
        void renderFogSettings();
};

