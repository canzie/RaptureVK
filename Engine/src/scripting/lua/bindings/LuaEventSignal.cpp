#include "LuaBindings.h"

#include "scripting/lua/LuaEventSignal.h"

namespace Rapture::scripting {

void registerEventSignalBindings(sol::state_view lua)
{
    lua.new_usertype<LuaEventSignal>("EventSignal", sol::no_constructor, "connect", &LuaEventSignal::connect, "once",
                                     &LuaEventSignal::once);

    lua.new_usertype<LuaEventConnection>("EventConnection", sol::no_constructor, "disconnect", &LuaEventConnection::disconnect,
                                         "connected", sol::property(&LuaEventConnection::connected));
}

} // namespace Rapture::scripting
