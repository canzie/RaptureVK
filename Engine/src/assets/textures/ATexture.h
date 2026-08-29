#ifndef RAPTURE__ATEXTURE_H
#define RAPTURE__ATEXTURE_H

#include "assets/asset_manager/Asset.h"
#include "gpu/textures/Texture.h"

#include <memory>

namespace Rapture {

/**
 * @brief An image asset, held as the texture it is sampled from
 */
class ATexture : public Asset {
  public:
    explicit ATexture(std::unique_ptr<Texture> texture);

    static const TypeInfo &staticType();
    const TypeInfo &type() const override;

    std::vector<uint8_t> serialize() const override;

    Texture &texture() { return *m_texture; }
    const Texture &texture() const { return *m_texture; }

    Texture *operator->() const { return m_texture.get(); }

  private:
    std::unique_ptr<Texture> m_texture;
};

} // namespace Rapture

#endif // RAPTURE__ATEXTURE_H
