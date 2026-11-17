#ifndef RAPTURE__COMMAND_REGISTRY_H
#define RAPTURE__COMMAND_REGISTRY_H

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Rapture {

/**
 * @brief The kinds of value a command takes
 */
enum CommandArgumentType {
    COMMAND_ARGUMENT_STRING,
    COMMAND_ARGUMENT_NUMBER,
    COMMAND_ARGUMENT_BOOL,
    COMMAND_ARGUMENT_TYPE_COUNT
};

/**
 * @brief One value written in a command call, held in the alternative its CommandArgumentType names
 */
using CommandArgument = std::variant<std::string, double, bool>;

/**
 * @brief A command call as written, for example workspace.select("World")
 */
struct CommandCall {
    std::string name;
    std::vector<CommandArgument> arguments;
};

/**
 * @brief Reads a command call out of a line of text
 * @param line The text, a dotted name followed by its arguments in parentheses
 * @param call Receives the call
 * @param error Receives what is wrong with the text when it cannot be read
 * @return True if the line holds one call and nothing else
 */
bool CommandCall_parse(std::string_view line, CommandCall &call, std::string &error);

/**
 * @brief A parameter a command declares
 */
struct CommandParameter {
    std::string name;
    CommandArgumentType type = COMMAND_ARGUMENT_STRING;
};

/**
 * @brief The arguments a command was called with, already checked against its parameters
 */
class CommandArguments {
  public:
    explicit CommandArguments(const std::vector<CommandArgument> &arguments) : m_arguments(arguments) {}

    std::string_view string(size_t index) const { return std::get<std::string>(m_arguments[index]); }
    double number(size_t index) const { return std::get<double>(m_arguments[index]); }
    bool boolean(size_t index) const { return std::get<bool>(m_arguments[index]); }

  private:
    const std::vector<CommandArgument> &m_arguments;
};

/**
 * @brief How a command ended
 */
enum CommandOutcome {
    COMMAND_SUCCEEDED,
    COMMAND_FAILED
};

/**
 * @brief Where a command's answer goes, given once, possibly after the command itself has returned
 */
class CommandReply {
  public:
    using Deliver = std::function<void(CommandOutcome outcome, std::string text)>;

    /**
     * @brief Creates a reply that hands its answer to whoever issued the command
     * @param deliver Receives the answer
     */
    explicit CommandReply(Deliver deliver) : m_deliver(std::move(deliver)) {}

    /**
     * @brief Answers with a failure if the command never answered, so the issuer is never left waiting
     */
    ~CommandReply();

    CommandReply(CommandReply &&other) noexcept;
    CommandReply &operator=(CommandReply &&other) noexcept;
    CommandReply(const CommandReply &) = delete;
    CommandReply &operator=(const CommandReply &) = delete;

    /**
     * @brief Answers that the command did what it was asked
     * @param text What the command has to say, may be empty
     */
    void succeed(std::string text = {});

    /**
     * @brief Answers that the command could not do what it was asked
     * @param text Why
     */
    void fail(std::string text);

  private:
    /**
     * @brief Hands an answer over, once
     * @param outcome How the command ended
     * @param text The answer's text
     */
    void deliver(CommandOutcome outcome, std::string text);

  private:
    Deliver m_deliver;
};

/**
 * @brief Runs a command with its checked arguments
 */
using CommandHandler = std::function<void(const CommandArguments &arguments, CommandReply reply)>;

/**
 * @brief Every command that can be issued as text, and what runs each one
 */
class CommandRegistry {
  public:
    /**
     * @brief Creates a registry holding help()
     */
    CommandRegistry();

    /**
     * @brief Adds a command
     * @param name The dotted name it is called by
     * @param description What it does, shown by help()
     * @param parameters What it takes, in order
     * @param handler What runs it
     */
    void add(std::string name, std::string description, std::vector<CommandParameter> parameters, CommandHandler handler);

    /**
     * @brief Runs the command a line of text calls
     * @param line The text, for example workspace.select("World")
     * @param reply Where the answer goes, failed straight away if the text calls nothing that exists
     */
    void execute(std::string_view line, CommandReply reply) const;

  private:
    struct Command {
        std::string description;
        std::vector<CommandParameter> parameters;
        CommandHandler handler;
    };

    /**
     * @brief Writes every command with its parameters and description
     * @return The listing, one command per line
     */
    std::string describeCommands() const;

  private:
    std::map<std::string, Command, std::less<>> m_commands;
};

} // namespace Rapture

#endif // RAPTURE__COMMAND_REGISTRY_H
