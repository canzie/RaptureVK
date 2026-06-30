#ifndef RAPTURE__SHADER_INPUTS_PANEL_H
#define RAPTURE__SHADER_INPUTS_PANEL_H

#include "layers/panels/Panel.h"

#include <amethyst/Amethyst.h>

#include <functional>
#include <memory>
#include <vector>

struct TextureGeneratorInstance;
namespace Rapture { class Shader; }

/**
 * @brief Panel that reflects a compute shader's push constant members as editable controls.
 */
class ShaderInputsPanel : public Panel {
  public:
    ShaderInputsPanel(Amethyst::TabBar *tabBar, const PanelServices &services);
    ~ShaderInputsPanel();

    /**
     * @brief Rebuild controls from the active instance's reflected shader.
     * @param onChanged Invoked after any parameter is edited.
     */
    void setInstance(TextureGeneratorInstance *instance, std::function<void()> onChanged);
    void clearInstance();

    static void applyShaderDefaults(std::vector<uint8_t> &buffer, const Rapture::Shader &shader);

  private:
    struct MemberState;

    void rebuild(TextureGeneratorInstance &instance, const std::function<void()> &onChanged);

    Amethyst::Frame *m_root = nullptr;
    Amethyst::ScrollingFrame *m_content = nullptr;

    std::vector<std::unique_ptr<MemberState>> m_memberStates;
};

#endif // RAPTURE__SHADER_INPUTS_PANEL_H
