#ifndef RAPTURE__AMODULE_H
#define RAPTURE__AMODULE_H

#include "assets/asset_manager/Asset.h"
#include "core/serialization/SerialDocument.h"

#include <memory>

namespace Rapture {

/**
 * @brief A module asset, held as the document the scene objects it describes are read from
 */
class AModule : public Asset {
  public:
    explicit AModule(std::unique_ptr<SerialDocument> document);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::vector<uint8_t> serialize() const override;

    SerialDocument &document() { return *m_document; }
    const SerialDocument &document() const { return *m_document; }

    SerialDocument *operator->() const { return m_document.get(); }

  private:
    std::unique_ptr<SerialDocument> m_document;
};

} // namespace Rapture

#endif // RAPTURE__AMODULE_H
