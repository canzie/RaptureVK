#ifndef RAPTURE__WORLD_SETTINGS_PANEL_H
#define RAPTURE__WORLD_SETTINGS_PANEL_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "Icons.h"
#include "inspector/PropertySections.h"
#include "panels/Panel.h"

#include <optional>

namespace Rapture {
class World;
} // namespace Rapture

/**
 * @brief The section holding what a world is played with.
 */
class WorldPlaySection : public PropertySection {
  public:
    const char *title() const override { return "Play"; }
    const char *icon() const override { return Icons::SVG_PLAY; }
    void buildBody(Amethyst::CollapsibleHeaderScope &ch) override;
    void sync() override;

  public:
    Rapture::World *world = nullptr;

  private:
    std::optional<AssetPicker> m_puppetPicker;
    std::optional<AssetPicker> m_controllerPicker;
    double m_gravity = -9.81;
};

/**
 * @brief Edits the settings of the world a workspace is open on.
 */
class WorldSettingsPanel : public Panel {
  public:
    std::string_view typeName() const override { return "WorldSettingsPanel"; }
    explicit WorldSettingsPanel(const WorkspaceContext &context);

    void onUpdate(float dt) override;

  protected:
    void build(Amethyst::UIScope &scope) override;
    void onRootDestroyed() override;

  private:
    void setupPlaceholder(Amethyst::UIScope &scope);
    void setupWorldView(void);
    void refresh(void);

  private:
    Amethyst::TextLabel *m_placeholderText = nullptr;
    std::optional<PropertySectionList> m_sections;

    Rapture::World *m_world = nullptr;
};

#endif // RAPTURE__WORLD_SETTINGS_PANEL_H
