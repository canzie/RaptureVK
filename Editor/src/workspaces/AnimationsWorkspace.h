#ifndef RAPTURE__ANIMATIONS_WORKSPACE_H
#define RAPTURE__ANIMATIONS_WORKSPACE_H

#include "Workspace.h"

class AnimationsWorkspace : public Workspace {
  public:
    static constexpr std::string_view LABEL = "Animations";

    explicit AnimationsWorkspace(const EditorContext &context) : Workspace(context) {}

    std::string_view tabLabel() const override { return LABEL; }

  protected:
    void build() override {}
    void buildHotbar() override {}
    std::string dockLayoutName() const override { return "Animations Dock"; }
};

#endif // RAPTURE__ANIMATIONS_WORKSPACE_H
