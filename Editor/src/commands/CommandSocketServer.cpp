#include "commands/CommandSocketServer.h"

#include <rapture/core/utils/Log.h>

#include <algorithm>
#include <cstdlib>

#if defined(__unix__)
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif // __unix__

#if defined(__unix__)

static constexpr int LISTEN_BACKLOG = 4;
static constexpr size_t RECEIVE_CHUNK_SIZE = 4096;

static bool s_sendAll(int socket, const std::string &bytes)
{
    size_t sent = 0;
    while (sent < bytes.size()) {
        ssize_t written = send(socket, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        sent += static_cast<size_t>(written);
    }

    return true;
}

CommandSocketServer::CommandSocketServer(const Rapture::CommandRegistry &registry, std::filesystem::path socketPath)
    : m_registry(&registry), m_socketPath(std::move(socketPath))
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = m_socketPath.string();
    if (path.size() >= sizeof(address.sun_path)) {
        RP_ERROR("'{}' is too long to be a socket path", path);
        return;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

    m_listenSocket = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_listenSocket < 0) {
        RP_ERROR("Could not create the command socket: {}", std::strerror(errno));
        return;
    }

    // a socket file left behind by a process that crashed would otherwise keep the bind from succeeding
    unlink(path.c_str());

    // only the current user may connect, the socket file takes its mode from this mask
    const mode_t previousMask = umask(0077);
    const int bound = bind(m_listenSocket, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    umask(previousMask);

    if (bound != 0 || listen(m_listenSocket, LISTEN_BACKLOG) != 0) {
        RP_ERROR("Could not listen on '{}': {}", path, std::strerror(errno));
        close(m_listenSocket);
        m_listenSocket = -1;
        return;
    }

    RP_INFO("Taking commands on '{}'", path);
}

CommandSocketServer::~CommandSocketServer()
{
    for (const auto &connection : m_connections) {
        close(connection->socket);
    }

    if (m_listenSocket >= 0) {
        close(m_listenSocket);
        unlink(m_socketPath.string().c_str());
    }
}

std::filesystem::path CommandSocketServer::defaultSocketPath()
{
    const char *runtimeDirectory = std::getenv("XDG_RUNTIME_DIR");
    std::filesystem::path folder = runtimeDirectory != nullptr ? std::filesystem::path(runtimeDirectory) : std::filesystem::path("/tmp");
    return folder / ("rapture-" + std::to_string(getpid()) + ".sock");
}

void CommandSocketServer::onUpdate()
{
    if (m_listenSocket < 0) {
        return;
    }

    acceptClients();

    for (size_t i = 0; i < m_connections.size(); i++) {
        if (!serviceConnection(*m_connections[i])) {
            close(m_connections[i]->socket);
            m_connections[i]->socket = -1;
        }
    }

    std::erase_if(m_connections, [](const std::unique_ptr<Connection> &connection) { return connection->socket < 0; });
}

void CommandSocketServer::acceptClients()
{
    while (true) {
        int client = accept4(m_listenSocket, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            return;
        }

        auto connection = std::make_unique<Connection>();
        connection->id = m_nextConnectionId++;
        connection->socket = client;
        m_connections.push_back(std::move(connection));
    }
}

bool CommandSocketServer::serviceConnection(Connection &connection)
{
    char chunk[RECEIVE_CHUNK_SIZE];
    while (true) {
        ssize_t received = recv(connection.socket, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (received > 0) {
            connection.received.append(chunk, static_cast<size_t>(received));
            continue;
        }
        if (received == 0) {
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return false;
        }
        break;
    }

    if (connection.isAwaitingAnswer) {
        return true;
    }

    const size_t lineEnd = connection.received.find('\n');
    if (lineEnd == std::string::npos) {
        return true;
    }

    std::string line = connection.received.substr(0, lineEnd);
    connection.received.erase(0, lineEnd + 1);
    connection.isAwaitingAnswer = true;

    const uint32_t connectionId = connection.id;
    m_registry->execute(line, Rapture::CommandReply([this, connectionId](Rapture::CommandResult result, std::string text) {
                            sendAnswer(connectionId, result, text);
                        }));
    return true;
}

void CommandSocketServer::sendAnswer(uint32_t connectionId, Rapture::CommandResult result, const std::string &text)
{
    Connection *connection = findConnection(connectionId);
    if (connection == nullptr) {
        return;
    }

    connection->isAwaitingAnswer = false;

    const std::string header = std::string(result == Rapture::COMMAND_RESULT_SUCCEEDED ? "ok " : "error ") + std::to_string(text.size()) + "\n";
    if (!s_sendAll(connection->socket, header + text)) {
        close(connection->socket);
        connection->socket = -1;
    }
}

CommandSocketServer::Connection *CommandSocketServer::findConnection(uint32_t connectionId)
{
    auto found = std::find_if(m_connections.begin(), m_connections.end(), [connectionId](const std::unique_ptr<Connection> &connection) {
        return connection->id == connectionId && connection->socket >= 0;
    });
    return found != m_connections.end() ? found->get() : nullptr;
}

#else

CommandSocketServer::CommandSocketServer(const Rapture::CommandRegistry &registry, std::filesystem::path socketPath)
    : m_registry(&registry), m_socketPath(std::move(socketPath))
{
    RP_WARN("Commands over a local socket are not available on this platform");
}

CommandSocketServer::~CommandSocketServer() = default;

std::filesystem::path CommandSocketServer::defaultSocketPath()
{
    return {};
}

void CommandSocketServer::onUpdate() {}

void CommandSocketServer::acceptClients() {}

bool CommandSocketServer::serviceConnection(Connection &connection)
{
    (void)connection;
    return false;
}

void CommandSocketServer::sendAnswer(uint32_t connectionId, Rapture::CommandResult result, const std::string &text)
{
    (void)connectionId;
    (void)result;
    (void)text;
}

CommandSocketServer::Connection *CommandSocketServer::findConnection(uint32_t connectionId)
{
    (void)connectionId;
    return nullptr;
}

#endif // __unix__
