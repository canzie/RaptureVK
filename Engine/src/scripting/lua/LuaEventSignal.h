#ifndef RAPTURE__LUAEVENTSIGNAL_H
#define RAPTURE__LUAEVENTSIGNAL_H

#include "core/events/EventSignal.h"
#include "core/utils/Log.h"
#include "scripting/lua/LuaHandle.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace Rapture::scripting {

/**
 * @brief Holds a connection for as long as the state it was made in lives
 */
class LuaEventConnectionStore {
  public:
    /**
     * @brief Takes ownership of a connection
     * @param connection The connection to hold
     * @return The connection, shared with whatever the script keeps
     */
    std::shared_ptr<EventConnection> keep(EventConnection connection);

    void clear() { m_connections.clear(); }

  private:
    void dropDisconnected();

    std::vector<std::shared_ptr<EventConnection>> m_connections;
    size_t m_nextPrune = 64;
};

/**
 * @brief What a script disconnects a callback through
 */
class LuaEventConnection {
  public:
    LuaEventConnection() = default;
    explicit LuaEventConnection(std::shared_ptr<EventConnection> connection)
        : m_connection(std::move(connection))
    {
    }

    void disconnect();
    bool connected() const;

  private:
    std::shared_ptr<EventConnection> m_connection;
};

/**
 * @brief What a script connects a callback to, standing in for a signal of any signature
 */
class LuaEventSignal {
  public:
    using Connector = std::function<EventConnection(const sol::protected_function &callback, bool once)>;

    LuaEventSignal() = default;
    LuaEventSignal(Instance *owner, Connector connect) : m_owner(owner), m_connect(std::move(connect)) {}

    /**
     * @brief Subscribes a callback
     * @param callback The function to call on every fire
     * @param state The state the call came from
     * @return The connection, held by the state until it is disconnected
     */
    LuaEventConnection connect(const sol::protected_function &callback, sol::this_state state);

    /**
     * @brief Subscribes a callback that is dropped after one fire
     * @param callback The function to call on the next fire
     * @param state The state the call came from
     * @return The connection, held by the state until it is disconnected
     */
    LuaEventConnection once(const sol::protected_function &callback, sol::this_state state);

  private:
    LuaEventConnection subscribe(const sol::protected_function &callback, sol::this_state state, bool once);

    LuaHandle<Instance> m_owner;
    Connector m_connect;
};

/**
 * @brief Installs the store a state's connections are kept in
 * @param lua The state to install into
 * @param store The store, which has to outlive the state
 */
void setLuaEventConnectionStore(sol::state_view lua, LuaEventConnectionStore &store);

/**
 * @brief Converts one fired argument into what a script receives for it
 * @param lua The state the callback runs in
 * @param value The argument as the signal declares it
 * @return A handle if the argument names an instance, the value itself otherwise
 */
template <typename T>
decltype(auto) toLuaArgument(sol::state_view lua, T &&value)
{
    using Bare = std::remove_cvref_t<T>;
    if constexpr (std::is_pointer_v<Bare> && std::is_base_of_v<Instance, std::remove_pointer_t<Bare>>) {
        return LuaHandleBase::push(lua, value);
    } else {
        return std::forward<T>(value);
    }
}

/**
 * @brief Wraps one of an instance's signals so a script can connect to it
 * @param owner The instance the signal belongs to, which the wrapper outlives
 * @param signal The signal to wrap
 * @return The wrapper, which does nothing once the owner is destroyed
 */
template <typename... Args>
LuaEventSignal makeLuaEventSignal(Instance *owner, EventSignal<void(Args...)> &signal)
{
    return LuaEventSignal(owner, [&signal](const sol::protected_function &callback, bool once) {
        auto handler = [callback](Args... args) {
            sol::state_view lua(callback.lua_state());
            sol::protected_function_result result = callback(toLuaArgument(lua, args)...);
            if (!result.valid()) {
                sol::error failure = result;
                RP_CORE_ERROR("{}", failure.what());
            }
        };

        return once ? signal.once(handler) : signal.connect(handler);
    });
}

} // namespace Rapture::scripting

#endif // RAPTURE__LUAEVENTSIGNAL_H
