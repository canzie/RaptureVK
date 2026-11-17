#include <rapture/core/commands/CommandRegistry.h>

#include <cctype>
#include <charconv>
#include <utility>

namespace Rapture {

static_assert(std::variant_size_v<CommandArgument> == COMMAND_ARGUMENT_TYPE_COUNT,
              "every argument type needs the CommandArgument alternative at its index");

static constexpr std::string_view COMMAND_ARGUMENT_TYPE_NAMES[COMMAND_ARGUMENT_TYPE_COUNT] = {"string", "number", "bool"};

/**
 * @brief Walks a line of text one character at a time while a command call is read out of it
 */
struct CommandTextCursor {
    std::string_view text;
    size_t position = 0;

    bool atEnd() const { return position >= text.size(); }
    char peek() const { return atEnd() ? '\0' : text[position]; }

    void skipWhitespace()
    {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(text[position])) != 0) {
            position++;
        }
    }
};

static bool s_isNameStart(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

static bool s_isNameCharacter(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

static bool s_readCommandName(CommandTextCursor &cursor, std::string &name, std::string &error)
{
    const size_t start = cursor.position;

    while (true) {
        if (!s_isNameStart(cursor.peek())) {
            error = "expected a command name at column " + std::to_string(cursor.position + 1);
            return false;
        }

        while (s_isNameCharacter(cursor.peek())) {
            cursor.position++;
        }

        if (cursor.peek() != '.') {
            break;
        }
        cursor.position++;
    }

    name = std::string(cursor.text.substr(start, cursor.position - start));
    return true;
}

static bool s_readStringArgument(CommandTextCursor &cursor, CommandArgument &argument, std::string &error)
{
    // past the opening quote
    cursor.position++;

    std::string value;
    while (!cursor.atEnd() && cursor.peek() != '"') {
        char c = cursor.text[cursor.position++];
        if (c != '\\') {
            value += c;
            continue;
        }

        if (cursor.atEnd()) {
            break;
        }

        char escaped = cursor.text[cursor.position++];
        switch (escaped) {
        case 'n':
            value += '\n';
            break;
        case 't':
            value += '\t';
            break;
        default:
            value += escaped;
            break;
        }
    }

    if (cursor.atEnd()) {
        error = "a string is never closed";
        return false;
    }

    cursor.position++;
    argument = std::move(value);
    return true;
}

static bool s_readWordArgument(CommandTextCursor &cursor, CommandArgument &argument, std::string &error)
{
    const size_t start = cursor.position;
    while (!cursor.atEnd() && cursor.peek() != ',' && cursor.peek() != ')' &&
           std::isspace(static_cast<unsigned char>(cursor.peek())) == 0) {
        cursor.position++;
    }

    std::string_view word = cursor.text.substr(start, cursor.position - start);
    if (word == "true" || word == "false") {
        argument = word == "true";
        return true;
    }

    double number = 0.0;
    auto [end, result] = std::from_chars(word.data(), word.data() + word.size(), number);
    if (result != std::errc() || end != word.data() + word.size() || word.empty()) {
        error = "'" + std::string(word) + "' is not a string, number or bool";
        return false;
    }

    argument = number;
    return true;
}

static bool s_readArguments(CommandTextCursor &cursor, std::vector<CommandArgument> &arguments, std::string &error)
{
    cursor.skipWhitespace();
    if (cursor.peek() == ')') {
        cursor.position++;
        return true;
    }

    while (true) {
        cursor.skipWhitespace();

        CommandArgument argument;
        const bool read = cursor.peek() == '"' ? s_readStringArgument(cursor, argument, error)
                                               : s_readWordArgument(cursor, argument, error);
        if (!read) {
            return false;
        }
        arguments.push_back(std::move(argument));

        cursor.skipWhitespace();
        if (cursor.peek() == ')') {
            cursor.position++;
            return true;
        }

        if (cursor.peek() != ',') {
            error = "expected ',' or ')' at column " + std::to_string(cursor.position + 1);
            return false;
        }
        cursor.position++;
    }
}

bool CommandCall_parse(std::string_view line, CommandCall &call, std::string &error)
{
    CommandTextCursor cursor{line};
    call = CommandCall{};

    cursor.skipWhitespace();
    if (!s_readCommandName(cursor, call.name, error)) {
        return false;
    }

    cursor.skipWhitespace();
    if (cursor.peek() != '(') {
        error = "expected '(' after " + call.name;
        return false;
    }
    cursor.position++;

    if (!s_readArguments(cursor, call.arguments, error)) {
        return false;
    }

    cursor.skipWhitespace();
    if (!cursor.atEnd()) {
        error = "unexpected text after the call at column " + std::to_string(cursor.position + 1);
        return false;
    }

    return true;
}

CommandReply::~CommandReply()
{
    deliver(COMMAND_FAILED, "the command gave no answer");
}

CommandReply::CommandReply(CommandReply &&other) noexcept : m_deliver(std::exchange(other.m_deliver, nullptr)) {}

CommandReply &CommandReply::operator=(CommandReply &&other) noexcept
{
    if (this != &other) {
        deliver(COMMAND_FAILED, "the command gave no answer");
        m_deliver = std::exchange(other.m_deliver, nullptr);
    }
    return *this;
}

void CommandReply::succeed(std::string text)
{
    deliver(COMMAND_SUCCEEDED, std::move(text));
}

void CommandReply::fail(std::string text)
{
    deliver(COMMAND_FAILED, std::move(text));
}

void CommandReply::deliver(CommandOutcome outcome, std::string text)
{
    if (m_deliver == nullptr) {
        return;
    }

    Deliver handOver = std::exchange(m_deliver, nullptr);
    handOver(outcome, std::move(text));
}

CommandRegistry::CommandRegistry()
{
    add("help", "Lists every command", {}, [this](const CommandArguments &arguments, CommandReply reply) {
        (void)arguments;
        reply.succeed(describeCommands());
    });
}

void CommandRegistry::add(std::string name, std::string description, std::vector<CommandParameter> parameters,
                          CommandHandler handler)
{
    m_commands.insert_or_assign(std::move(name), Command{std::move(description), std::move(parameters), std::move(handler)});
}

void CommandRegistry::execute(std::string_view line, CommandReply reply) const
{
    CommandCall call;
    std::string error;
    if (!CommandCall_parse(line, call, error)) {
        reply.fail(error);
        return;
    }

    auto found = m_commands.find(call.name);
    if (found == m_commands.end()) {
        reply.fail("no command named " + call.name + ", help() lists them");
        return;
    }

    const Command &command = found->second;
    if (call.arguments.size() != command.parameters.size()) {
        reply.fail(call.name + " takes " + std::to_string(command.parameters.size()) + " arguments, " +
                   std::to_string(call.arguments.size()) + " were given");
        return;
    }

    for (size_t i = 0; i < call.arguments.size(); i++) {
        const CommandParameter &parameter = command.parameters[i];
        if (call.arguments[i].index() != static_cast<size_t>(parameter.type)) {
            reply.fail(parameter.name + " has to be a " + std::string(COMMAND_ARGUMENT_TYPE_NAMES[parameter.type]));
            return;
        }
    }

    command.handler(CommandArguments(call.arguments), std::move(reply));
}

std::string CommandRegistry::describeCommands() const
{
    std::string listing;
    for (const auto &[name, command] : m_commands) {
        listing += name + "(";
        for (size_t i = 0; i < command.parameters.size(); i++) {
            if (i > 0) {
                listing += ", ";
            }
            listing += command.parameters[i].name + ": " + std::string(COMMAND_ARGUMENT_TYPE_NAMES[command.parameters[i].type]);
        }
        listing += ")  " + command.description + "\n";
    }

    return listing;
}

} // namespace Rapture
