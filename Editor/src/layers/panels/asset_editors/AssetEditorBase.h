#ifndef RAPTURE__ASSET_EDITOR_BASE_H
#define RAPTURE__ASSET_EDITOR_BASE_H

#include <amethyst/Amethyst.h>
#include <components/ui_scope.h>

#include "layers/panels/components/property_sections.h"

#include <assets/asset_manager/AssetCommon.h>

/**
 * @brief A property section that edits one facet of the open asset.
 */
class AssetEditorBase : public PropertySection {
  public:
    void sync() override
    {
        sync(m_handle);
        m_subjectChanged = false;
    }

    /**
     * @brief Pushes the asset's data into the bound widget buffers.
     * @param handle The asset this section edits.
     */
    virtual void sync(Rapture::AssetHandle handle) = 0;

    /**
     * @brief Points this editor at what it edits, before its body is built.
     * @param handle The asset this editor edits.
     */
    virtual void setSubject(Rapture::AssetHandle handle)
    {
        m_subjectChanged = m_subjectChanged || handle != m_handle;
        m_handle = handle;
    }

  protected:
    /**
     * @brief Whether this editor was pointed at a different asset since it last synced.
     * @return True while the widgets still hold the previous asset's values.
     */
    bool subjectChanged() const { return m_subjectChanged; }

  protected:
    Rapture::AssetHandle m_handle = Rapture::INVALID_ASSET_HANDLE;
    bool m_subjectChanged = true;
};

#endif // RAPTURE__ASSET_EDITOR_BASE_H
