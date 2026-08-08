#ifndef RAPTURE__MODULE_EDITOR_BASE_H
#define RAPTURE__MODULE_EDITOR_BASE_H

#include "layers/panels/components/property_sections.h"
#include "modules/ModuleClass.h"

/**
 * @brief A property section that edits the fields one class in the open module's ancestry declares.
 */
class ModuleEditorBase : public PropertySection {
  public:
    void sync() override { sync(module); }

    /**
     * @brief Pushes the module's fields into the bound widget buffers.
     * @param module The module this section edits.
     */
    virtual void sync(Rapture::ModuleClass *module) = 0;

  public:
    Rapture::ModuleClass *module = nullptr;
};

#endif // RAPTURE__MODULE_EDITOR_BASE_H
