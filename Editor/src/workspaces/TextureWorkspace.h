#ifndef RAPTURE__TEXTURE_WORKSPACE_H
#define RAPTURE__TEXTURE_WORKSPACE_H

#include "AssetWorkspace.h"

/**
 * @brief The workspace one open texture asset is inspected in
 */
class TextureWorkspace : public AssetWorkspace {
  public:
    std::string_view typeName() const override { return "TextureWorkspace"; }
    TextureWorkspace(const EditorContext &context, Rapture::AssetHandle handle);

    std::string_view tabIcon() const override;

  protected:
    void build() override;
    void buildHotbar() override {}
};

#endif // RAPTURE__TEXTURE_WORKSPACE_H
