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
 * @brief The kinds of value a command parameter takes
 */
enum CommandArgumentType {
    COMMAND_ARGUMENT_TYPE_STRING,
    COMMAND_ARGUMENT_TYPE_NUMBER,
    COMMAND_ARGUMENT_TYPE_BOOL,
    COMMAND_ARGUMENT_TYPE_CHOICE,
    COMMAND_ARGUMENT_TYPE_COUNT
};

/**
 * @brief How a parameter is written in a call
 */
enum CommandParameterKind {
    COMMAND_PARAMETER_KIND_POSITIONAL, ///< a word in order, always given
    COMMAND_PARAMETER_KIND_OPTION,     ///< --name followed by a value, or alone for a bool
    COMMAND_PARAMETER_KIND_REMAINING,  ///< every word left over, joined by spaces, may be empty
    COMMAND_PARAMETER_KIND_COUNT
};

/**
 * @brief A value a call was given, a choice is held as its string
 */
using CommandArgument = std::variant<std::string, double, bool>;

/**
 * @brief The values a call was given, by parameter name, already checked against the command's parameters
 */
class CommandArguments {
  public:
    /**
     * @brief Whether a parameter was given
     * @param name The parameter's name
     * @return True if it was given, always true for a positional parameter of a command being run
     */
    bool has(std::string_view name) const;

    /**
     * @brief A string or choice parameter's value
     * @param name The parameter's name, which has to have been given
     * @return The value
     */
    std::string_view string(std::string_view name) const;

    /**
     * @brief A number parameter's value
     * @param name The parameter's name, which has to have been given
     * @return The value
     */
    double number(std::string_view name) const;

    /**
     * @brief A bool parameter's value
     * @param name The parameter's name
     * @return The value, false for an option that was not given
     */
    bool boolean(std::string_view name) const;

    /**
     * @brief Records a parameter's value
     * @param name The parameter's name
     * @param value The value
     */
    void set(std::string name, CommandArgument value);

  private:
    std::map<std::string, CommandArgument, std::less<>> m_values;
};

/**
 * @brief One word a partly typed line can continue with, and what it stands for
 */
struct CommandCompletion {
    std::string text;
    std::string description;
};

/**
 * @brief Lists the values a parameter can currently take, given what the call already holds
 */
using CommandCompleter = std::function<std::vector<std::string>(const CommandArguments &given)>;

/**
 * @brief A parameter a command declares
 */
struct CommandParameter {
    std::string name;
    CommandArgumentType type = COMMAND_ARGUMENT_TYPE_STRING;
    CommandParameterKind kind = COMMAND_PARAMETER_KIND_POSITIONAL;
    std::string description;
    std::vector<std::string> choices = {};
    CommandCompleter complete = {};
};

/**
 * @brief How a command ended
 */
enum CommandResult {
    COMMAND_RESULT_SUCCEEDED,
    COMMAND_RESULT_FAILED
};

/**
 * @brief Where a command's answer goes, given once, possibly after the command itself has returned
 */
class CommandReply {
  public:
    using Deliver = std::function<void(CommandResult result, std::string text)>;

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
     * @param result How the command ended
     * @param text The answer's text
     */
    void deliver(CommandResult result, std::string text);

  private:
    Deliver m_deliver;
};

/**
 * @brief Runs a command with its checked arguments
 */
using CommandHandler = std::function<void(const CommandArguments &arguments, CommandReply reply)>;

/**
 * @brief Everything a command is: what it is called, what it does, what it takes and what runs it
 */
struct CommandDescription {
    std::string name;
    std::string description;
    std::vector<CommandParameter> parameters = {};
    CommandHandler run;
};

/**
 * @brief A named set of commands and of further groups, the words of a call before the command's own
 */
class CommandGroup {
  public:
    explicit CommandGroup(std::string description = {}) : m_description(std::move(description)) {}

    /**
     * @brief The group below this one with a name, created the first time it is asked for
     * @param name The group's name, which no command in this group may have
     * @param description What the group holds, shown by help
     * @return The group
     */
    CommandGroup &group(std::string_view name, std::string description);

    /**
     * @brief Adds a command to this group
     * @param command The command, whose name no group or command in this group may have
     */
    void add(CommandDescription command);

    const std::string &description() const { return m_description; }

    /**
     * @brief A group directly below this one
     * @param name The group's name
     * @return The group, or nullptr if this group holds none by that name
     */
    const CommandGroup *findGroup(std::string_view name) const;

    /**
     * @brief A command directly in this group
     * @param name The command's name
     * @return The command, or nullptr if this group holds none by that name
     */
    const CommandDescription *findCommand(std::string_view name) const;

    const std::map<std::string, CommandGroup, std::less<>> &groups() const { return m_groups; }
    const std::map<std::string, CommandDescription, std::less<>> &commands() const { return m_commands; }

  private:
    std::string m_description;
    std::map<std::string, CommandGroup, std::less<>> m_groups;
    std::map<std::string, CommandDescription, std::less<>> m_commands;
};

/**
 * @brief Every command that can be issued as text, and what runs each one
 */
class CommandRegistry {
  public:
    /**
     * @brief Creates a registry holding help and complete
     */
    CommandRegistry();

    /**
     * @brief A top level group, created the first time it is asked for
     * @param name The group's name
     * @param description What the group holds, shown by help
     * @return The group
     */
    CommandGroup &group(std::string_view name, std::string description) { return m_root.group(name, std::move(description)); }

    /**
     * @brief Adds a top level command
     * @param command The command
     */
    void add(CommandDescription command) { m_root.add(std::move(command)); }

    /**
     * @brief Runs the command a line of text calls
     * @param line The words naming the command, followed by its arguments
     * @param reply Where the answer goes, failed straight away if the line calls nothing that exists or is malformed
     */
    void execute(std::string_view line, CommandReply reply) const;

    /**
     * @brief Lists what the last word of a partly typed line can become
     * @param line The line as typed so far, ending in the word being completed or in a space to start a new one
     * @return The candidates for that word, each with what it stands for
     */
    std::vector<CommandCompletion> complete(std::string_view line) const;

    /**
     * @brief Describes a group or command
     * @param path The words leading to it, empty for the top level
     * @return The description, listing a group's contents or a command's parameters
     */
    std::string describe(std::string_view path) const;

  private:
    CommandGroup m_root;
};

} // namespace Rapture

#endif // RAPTURE__COMMAND_REGISTRY_H
