#include "AMaterialInstance.h"

namespace Rapture {

AMaterialInstance::AMaterialInstance(std::unique_ptr<MaterialInstance> material) : m_material(std::move(material)) {}

const TypeInfo &AMaterialInstance::staticType()
{
    static const TypeInfo type("AMaterialInstance", &Asset::staticType());
    return type;
}

const TypeInfo &AMaterialInstance::type() const
{
    return staticType();
}

std::vector<uint8_t> AMaterialInstance::serialize() const
{
    return m_material->serialize();
}

} // namespace Rapture
