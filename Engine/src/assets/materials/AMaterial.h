#ifndef RAPTURE__AMATERIAL_H
#define RAPTURE__AMATERIAL_H

#include "assets/asset_manager/Asset.h"
#include "assets/materials/Material.h"

#include <memory>

namespace Rapture {

/**
 * @brief A material asset, the surface every instance of it is authored against
 */
class AMaterial : public Asset {
  public:
    explicit AMaterial(std::unique_ptr<BaseMaterial> material);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::vector<uint8_t> serialize() const override;

    BaseMaterial &material() { return *m_material; }
    const BaseMaterial &material() const { return *m_material; }

    BaseMaterial *operator->() const { return m_material.get(); }

  private:
    std::unique_ptr<BaseMaterial> m_material;
};

} // namespace Rapture

#endif // RAPTURE__AMATERIAL_H
