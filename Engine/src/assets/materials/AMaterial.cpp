#include "AMaterial.h"

namespace Rapture {

AMaterial::AMaterial(std::unique_ptr<BaseMaterial> material) : m_material(std::move(material)) {}

const TypeInfo &AMaterial::staticType()
{
    static const TypeInfo type("AMaterial", &Asset::staticType());
    return type;
}

const TypeInfo &AMaterial::type() const
{
    return staticType();
}

std::vector<uint8_t> AMaterial::serialize() const
{
    return m_material->serialize();
}

} // namespace Rapture
