#include "ATexture.h"

namespace Rapture {

ATexture::ATexture(std::unique_ptr<Texture> texture) : m_texture(std::move(texture)) {}

const TypeInfo &ATexture::staticType()
{
    static const TypeInfo type("ATexture", &Asset::staticType());
    return type;
}

const TypeInfo &ATexture::type() const
{
    return staticType();
}

std::vector<uint8_t> ATexture::serialize() const
{
    return m_texture->serialize();
}

} // namespace Rapture
