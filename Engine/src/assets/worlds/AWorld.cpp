#include "AWorld.h"

namespace Rapture {

AWorld::AWorld(std::unique_ptr<World> world) : m_world(std::move(world)) {}

const TypeInfo &AWorld::staticType()
{
    static const TypeInfo type("AWorld", &Asset::staticType());
    return type;
}

const TypeInfo &AWorld::type() const
{
    return staticType();
}

std::vector<uint8_t> AWorld::serialize() const
{
    return m_world->serialize();
}

} // namespace Rapture
