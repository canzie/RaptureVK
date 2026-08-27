#include "LuaEventSignal.h"

namespace Rapture::scripting {

static constexpr const char *STORE_REGISTRY_KEY = "rapture.connections";

std::shared_ptr<EventConnection> LuaEventConnectionStore::keep(EventConnection connection)
{
    if (m_connections.size() >= m_nextPrune) {
        dropDisconnected();
        m_nextPrune = m_connections.size() * 2 + 64;
    }

    auto held = std::make_shared<EventConnection>(std::move(connection));
    m_connections.push_back(held);
    return held;
}

void LuaEventConnectionStore::dropDisconnected()
{
    std::erase_if(m_connections, [](const std::shared_ptr<EventConnection> &connection) {
        return connection.use_count() == 1 && !connection->connected();
    });
}

void LuaEventConnection::disconnect()
{
    if (m_connection != nullptr) {
        m_connection->disconnect();
    }
}

bool LuaEventConnection::connected() const
{
    return m_connection != nullptr && m_connection->connected();
}

void setLuaEventConnectionStore(sol::state_view lua, LuaEventConnectionStore &store)
{
    lua.registry()[STORE_REGISTRY_KEY] = sol::light(store);
}

LuaEventConnection LuaEventSignal::connect(const sol::protected_function &callback, sol::this_state state)
{
    return subscribe(callback, state, false);
}

LuaEventConnection LuaEventSignal::once(const sol::protected_function &callback, sol::this_state state)
{
    return subscribe(callback, state, true);
}

LuaEventConnection LuaEventSignal::subscribe(const sol::protected_function &callback, sol::this_state state, bool once)
{
    if (m_owner.get() == nullptr || m_connect == nullptr) {
        luaL_error(state, "the object this signal belongs to has been destroyed");
    }

    if (!callback.valid()) {
        luaL_error(state, "expected a function to connect");
    }

    sol::state_view lua(state);
    sol::optional<sol::light<LuaEventConnectionStore>> store =
        lua.registry()[STORE_REGISTRY_KEY].get<sol::optional<sol::light<LuaEventConnectionStore>>>();
    if (!store.has_value()) {
        luaL_error(state, "this state holds no connection store");
    }

    return LuaEventConnection(store->value()->keep(m_connect(callback, once)));
}

} // namespace Rapture::scripting
