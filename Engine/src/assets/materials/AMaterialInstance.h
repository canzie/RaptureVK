#ifndef RAPTURE__AMATERIALINSTANCE_H
#define RAPTURE__AMATERIALINSTANCE_H

#include "assets/asset_manager/Asset.h"
#include "assets/materials/MaterialInstance.h"

#include <memory>

namespace Rapture {

/**
 * @brief A material instance asset, one set of values for the material it is an instance of
 */
class AMaterialInstance : public Asset {
  public:
    explicit AMaterialInstance(std::unique_ptr<MaterialInstance> material);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::vector<uint8_t> serialize() const override;

    MaterialInstance &material() { return *m_material; }
    const MaterialInstance &material() const { return *m_material; }

    MaterialInstance *operator->() const { return m_material.get(); }

  private:
    std::unique_ptr<MaterialInstance> m_material;
};

} // namespace Rapture

#endif // RAPTURE__AMATERIALINSTANCE_H
