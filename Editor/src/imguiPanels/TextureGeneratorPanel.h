#ifndef RAPTURE__TEXTUREGENERATORPANEL_H
#define RAPTURE__TEXTUREGENERATORPANEL_H

#include "Generators/Textures/ProceduralTextures.h"

#include <memory>
#include <string>
#include <vector>

struct TextureGeneratorInstance;

class TextureGeneratorPanel {
  public:
    TextureGeneratorPanel();
    ~TextureGeneratorPanel();

    void render();

  private:
    void renderInstanceSelector();
    void renderParameterEditor();
    void renderGenerateButton();

    std::vector<TextureGeneratorInstance> m_instances;
    int m_selectedIndex = -1;
};

#endif // RAPTURE__TEXTUREGENERATORPANEL_H
