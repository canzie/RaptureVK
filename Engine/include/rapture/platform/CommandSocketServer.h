#ifndef RAPTURE__COMMAND_SOCKET_SERVER_H
#define RAPTURE__COMMAND_SOCKET_SERVER_H

#include <rapture/core/commands/CommandRegistry.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Rapture {

/**
 * @brief Takes command calls from other processes on this machine over a local socket.
 *
 * A client writes one call per line and gets each answer back before its next call is read. An
 * answer is a header line, "ok <bytes>" or "error <bytes>", followed by that many bytes of text.
 */
class CommandSocketServer {
  public:
    /**
     * @brief Opens the socket, which only the current user can connect to
     * @param registry The commands calls are run against, which has to outlive the server
     * @param socketPath Where the socket is created
     */
    CommandSocketServer(const CommandRegistry &registry, std::filesystem::path socketPath);
    ~CommandSocketServer();

    CommandSocketServer(const CommandSocketServer &) = delete;
    CommandSocketServer &operator=(const CommandSocketServer &) = delete;

    /**
     * @brief Where this process puts its socket unless told otherwise
     * @return A path in the user's runtime directory, named after this process
     */
    static std::filesystem::path defaultSocketPath();

    /**
     * @brief Accepts new clients and runs every call that has arrived, without waiting for any
     */
    void poll();

    const std::filesystem::path &socketPath() const { return m_socketPath; }

  private:
    struct Connection {
        uint32_t id = 0;
        int socket = -1;
        std::string received;
        bool isAwaitingAnswer = false;
    };

    void acceptClients();

    /**
     * @brief Reads what a client sent and runs its next call
     * @param connection The client
     * @return False once the client has gone
     */
    bool serviceConnection(Connection &connection);

    /**
     * @brief Sends an answer to a client, if it is still connected
     * @param connectionId The client the call came from
     * @param outcome How the command ended
     * @param text The answer's text
     */
    void sendAnswer(uint32_t connectionId, CommandOutcome outcome, const std::string &text);

    Connection *findConnection(uint32_t connectionId);

  private:
    const CommandRegistry *m_registry;
    std::filesystem::path m_socketPath;
    int m_listenSocket = -1;
    std::vector<std::unique_ptr<Connection>> m_connections;
    uint32_t m_nextConnectionId = 1;
};

} // namespace Rapture

#endif // RAPTURE__COMMAND_SOCKET_SERVER_H
