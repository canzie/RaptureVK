#include "PreviewWorkspace.h"

PreviewWorkspace::PreviewWorkspace(const EditorContext &context, Rapture::AssetHandle handle,
                                   std::string_view previewSceneName)
    : AssetWorkspace(context, handle), m_preview(previewSceneName)
{
    m_context.scene = m_preview.scene();
}

void PreviewWorkspace::onUpdate(float dt)
{
    Workspace::onUpdate(dt);

    if (isActive()) {
        m_preview.onUpdate(dt);
    }
}
