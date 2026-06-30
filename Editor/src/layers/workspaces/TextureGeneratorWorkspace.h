#ifndef RAPTURE__TEXTURE_GENERATOR_WORKSPACE_H
#define RAPTURE__TEXTURE_GENERATOR_WORKSPACE_H

#include "Workspace.h"
#include "asset_manager/AssetCommon.h"
#include "generators/textures/ProceduralTextures.h"

#include <amethyst/Amethyst.h>

#include <memory>
#include <string>
#include <vector>

struct TextureGeneratorInstance {
    std::string name;
    uint32_t width = 1024;
    uint32_t height = 1024;
    Rapture::AssetHandle shaderHandle;
    std::vector<uint8_t> buffer;
    std::unique_ptr<Rapture::ProceduralTexture> generator;
    Amethyst::AmTextureId previewAmTexId;
};

/**
 * @brief Workspace for generating textures via compute shaders.
 */
class TextureGeneratorWorkspace : public Workspace {
  public:
    TextureGeneratorWorkspace(Amethyst::TabBarScope &tabs, const PanelServices &services);
    ~TextureGeneratorWorkspace() override = default;

    void saveLayout() override;

  private:
    void setupHotbar();
    void rebuildInstanceDropdown();
    void refreshShaderDropdown();
    void selectInstance(size_t index);
    void createInstance(const std::string &name, uint32_t width, uint32_t height, Rapture::AssetHandle shaderHandle);
    void generate();

    std::vector<std::unique_ptr<TextureGeneratorInstance>> m_instances;
    int m_selectedIndex = -1;
    bool m_autoGenerate = false;
    Rapture::AssetHandle m_pendingShaderHandle;

    Amethyst::Dropdown *m_instanceDropdown = nullptr;
    Amethyst::TextButton *m_newBtn = nullptr;
    Amethyst::Popup *m_newPopup = nullptr;
    Amethyst::TextInput *m_nameInput = nullptr;
    Amethyst::NumberInput *m_widthInput = nullptr;
    Amethyst::NumberInput *m_heightInput = nullptr;
    Amethyst::Dropdown *m_shaderDropdown = nullptr;
};

#endif // RAPTURE__TEXTURE_GENERATOR_WORKSPACE_H
