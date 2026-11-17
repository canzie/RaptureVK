#include <rapture/core/commands/CommandRegistry.h>

#include <rapture/core/utils/rp_assert.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <span>
#include <utility>

namespace Rapture {

static constexpr std::string_view COMMAND_ARGUMENT_TYPE_NAMES[COMMAND_ARGUMENT_TYPE_COUNT] = {"string", "number", "bool", "choice"};
static constexpr std::string_view OPTION_PREFIX = "--";

/**
 * @brief One word of a line, with whether it was written in quotes, which keeps it from being read as an option
 */
struct CommandWord {
    std::string text;
    bool quoted = false;
};

/**
 * @brief A line split into words
 */
struct CommandWords {
    std::vector<CommandWord> words;
    bool endsInsideWord = false;
    bool hasUnclosedQuote = false;
};

/**
 * @brief Where the words of a call lead in the tree
 */
struct CommandLocation {
    const CommandGroup *group = nullptr;
    const CommandDescription *command = nullptr;
    std::string path;
    size_t argumentStart = 0;
};

/**
 * @brief The arguments read out of a call's words so far
 */
struct CommandBinding {
    CommandArguments arguments;
    size_t positionalsFilled = 0;
    const CommandParameter *optionAwaitingValue = nullptr;
    std::string remaining;
};

static CommandWords s_splitIntoWords(std::string_view line)
{
    CommandWords split;
    size_t position = 0;

    while (position < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[position])) != 0) {
            position++;
            continue;
        }

        CommandWord word;
        if (line[position] == '"') {
            word.quoted = true;
            position++;
            while (position < line.size() && line[position] != '"') {
                if (line[position] == '\\' && position + 1 < line.size()) {
                    position++;
                }
                word.text += line[position++];
            }

            if (position >= line.size()) {
                split.hasUnclosedQuote = true;
            } else {
                position++;
            }
        } else {
            while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])) == 0) {
                word.text += line[position++];
            }
        }

        split.words.push_back(std::move(word));
    }

    split.endsInsideWord = !line.empty() && (split.hasUnclosedQuote || std::isspace(static_cast<unsigned char>(line.back())) == 0);
    return split;
}

static bool s_isOptionWord(const CommandWord &word)
{
    return !word.quoted && word.text.starts_with(OPTION_PREFIX) && word.text.size() > OPTION_PREFIX.size();
}

static std::string s_listGroupContents(const CommandGroup &group)
{
    std::string names;
    for (const auto &[name, child] : group.groups()) {
        names += (names.empty() ? "" : ", ") + name;
    }
    for (const auto &[name, command] : group.commands()) {
        names += (names.empty() ? "" : ", ") + name;
    }
    return names;
}

static std::string s_joinPath(const std::string &path, const std::string &word)
{
    return path.empty() ? word : path + " " + word;
}

/**
 * @brief Follows words down the tree until one names a command
 * @param root The top of the tree
 * @param words The words to follow
 * @param wordCount How many of the words may be followed
 * @param location Receives where the words lead
 * @param error Receives what is wrong when a word names nothing
 * @return True if the words reached a command or ran out at a group, false if one named nothing
 */
static bool s_followWords(const CommandGroup &root, const std::vector<CommandWord> &words, size_t wordCount, CommandLocation &location,
                          std::string &error)
{
    location.group = &root;

    for (size_t i = 0; i < wordCount; i++) {
        const std::string &word = words[i].text;

        if (const CommandGroup *child = location.group->findGroup(word)) {
            location.group = child;
            location.path = s_joinPath(location.path, word);
            continue;
        }

        if (const CommandDescription *command = location.group->findCommand(word)) {
            location.command = command;
            location.path = s_joinPath(location.path, word);
            location.argumentStart = i + 1;
            return true;
        }

        const std::string where = location.path.empty() ? "there is" : location.path + " has";
        error = where + " no '" + word + "', the choices are: " + s_listGroupContents(*location.group);
        return false;
    }

    return true;
}

static const CommandParameter *s_findOption(const CommandDescription &command, std::string_view name)
{
    for (const CommandParameter &parameter : command.parameters) {
        if (parameter.kind == COMMAND_PARAMETER_KIND_OPTION && parameter.name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

static const CommandParameter *s_findPositional(const CommandDescription &command, size_t index)
{
    size_t seen = 0;
    for (const CommandParameter &parameter : command.parameters) {
        if (parameter.kind != COMMAND_PARAMETER_KIND_POSITIONAL) {
            continue;
        }
        if (seen++ == index) {
            return &parameter;
        }
    }
    return nullptr;
}

static const CommandParameter *s_findRemaining(const CommandDescription &command)
{
    for (const CommandParameter &parameter : command.parameters) {
        if (parameter.kind == COMMAND_PARAMETER_KIND_REMAINING) {
            return &parameter;
        }
    }
    return nullptr;
}

static bool s_convertWord(const CommandParameter &parameter, const std::string &word, CommandArgument &value, std::string &error)
{
    switch (parameter.type) {
    case COMMAND_ARGUMENT_TYPE_STRING:
        value = word;
        return true;
    case COMMAND_ARGUMENT_TYPE_NUMBER: {
        double number = 0.0;
        auto [end, result] = std::from_chars(word.data(), word.data() + word.size(), number);
        if (result != std::errc() || end != word.data() + word.size() || word.empty()) {
            error = parameter.name + " has to be a number, '" + word + "' is not";
            return false;
        }
        value = number;
        return true;
    }
    case COMMAND_ARGUMENT_TYPE_BOOL:
        if (word != "true" && word != "false") {
            error = parameter.name + " has to be true or false, '" + word + "' is neither";
            return false;
        }
        value = word == "true";
        return true;
    case COMMAND_ARGUMENT_TYPE_CHOICE: {
        if (std::find(parameter.choices.begin(), parameter.choices.end(), word) == parameter.choices.end()) {
            std::string choices;
            for (const std::string &choice : parameter.choices) {
                choices += (choices.empty() ? "" : ", ") + choice;
            }
            error = parameter.name + " has to be one of " + choices + ", '" + word + "' is not";
            return false;
        }
        value = word;
        return true;
    }
    case COMMAND_ARGUMENT_TYPE_COUNT:
        break;
    }

    error = parameter.name + " has no type";
    return false;
}

static bool s_bindOption(const CommandDescription &command, const CommandWord &word, CommandBinding &binding, std::string &error)
{
    std::string_view name = std::string_view(word.text).substr(OPTION_PREFIX.size());
    const CommandParameter *option = s_findOption(command, name);
    if (option == nullptr) {
        error = "there is no option --" + std::string(name);
        return false;
    }

    if (binding.arguments.has(option->name)) {
        error = "--" + option->name + " is given twice";
        return false;
    }

    if (option->type == COMMAND_ARGUMENT_TYPE_BOOL) {
        binding.arguments.set(option->name, true);
        return true;
    }

    binding.optionAwaitingValue = option;
    return true;
}

static bool s_bindPositional(const CommandDescription &command, const CommandWord &word, CommandBinding &binding, std::string &error)
{
    if (const CommandParameter *positional = s_findPositional(command, binding.positionalsFilled)) {
        CommandArgument value;
        if (!s_convertWord(*positional, word.text, value, error)) {
            return false;
        }
        binding.arguments.set(positional->name, std::move(value));
        binding.positionalsFilled++;
        return true;
    }

    if (s_findRemaining(command) != nullptr) {
        binding.remaining += (binding.remaining.empty() ? "" : " ") + word.text;
        return true;
    }

    error = "'" + word.text + "' is one word too many";
    return false;
}

/**
 * @brief Reads a command's arguments out of the words after its name, stopping at the first that does not fit
 * @param command The command
 * @param words The words after its name
 * @param binding Receives what was read
 * @param error Receives what does not fit
 * @return True if every word fit
 */
static bool s_bindWords(const CommandDescription &command, std::span<const CommandWord> words, CommandBinding &binding,
                        std::string &error)
{
    for (const CommandWord &word : words) {
        if (binding.optionAwaitingValue != nullptr) {
            CommandArgument value;
            if (!s_convertWord(*binding.optionAwaitingValue, word.text, value, error)) {
                return false;
            }
            binding.arguments.set(binding.optionAwaitingValue->name, std::move(value));
            binding.optionAwaitingValue = nullptr;
            continue;
        }

        const bool bound = s_isOptionWord(word) ? s_bindOption(command, word, binding, error) : s_bindPositional(command, word, binding, error);
        if (!bound) {
            return false;
        }
    }

    return true;
}

static bool s_finishBinding(const CommandDescription &command, CommandBinding &binding, std::string &error)
{
    if (binding.optionAwaitingValue != nullptr) {
        error = "--" + binding.optionAwaitingValue->name + " needs a value";
        return false;
    }

    if (const CommandParameter *missing = s_findPositional(command, binding.positionalsFilled)) {
        error = "missing " + missing->name;
        return false;
    }

    if (const CommandParameter *remaining = s_findRemaining(command)) {
        binding.arguments.set(remaining->name, binding.remaining);
    }

    return true;
}

static std::vector<std::string> s_candidatesForParameter(const CommandParameter &parameter, const CommandArguments &given)
{
    if (parameter.type == COMMAND_ARGUMENT_TYPE_CHOICE) {
        return parameter.choices;
    }
    if (parameter.type == COMMAND_ARGUMENT_TYPE_BOOL) {
        return {"true", "false"};
    }
    if (parameter.complete != nullptr) {
        return parameter.complete(given);
    }
    return {};
}

static std::vector<std::string> s_candidatesForCommand(const CommandDescription &command, const CommandBinding &binding,
                                                       std::string_view partial)
{
    if (binding.optionAwaitingValue != nullptr) {
        return s_candidatesForParameter(*binding.optionAwaitingValue, binding.arguments);
    }

    if (partial.starts_with(OPTION_PREFIX)) {
        std::vector<std::string> options;
        for (const CommandParameter &parameter : command.parameters) {
            if (parameter.kind == COMMAND_PARAMETER_KIND_OPTION && !binding.arguments.has(parameter.name)) {
                options.push_back(std::string(OPTION_PREFIX) + parameter.name);
            }
        }
        return options;
    }

    if (const CommandParameter *positional = s_findPositional(command, binding.positionalsFilled)) {
        return s_candidatesForParameter(*positional, binding.arguments);
    }

    if (const CommandParameter *remaining = s_findRemaining(command)) {
        return s_candidatesForParameter(*remaining, binding.arguments);
    }

    return {};
}

static std::string s_describeUsage(const std::string &path, const CommandDescription &command)
{
    std::string usage = path;
    for (const CommandParameter &parameter : command.parameters) {
        switch (parameter.kind) {
        case COMMAND_PARAMETER_KIND_POSITIONAL:
            usage += " <" + parameter.name + ">";
            break;
        case COMMAND_PARAMETER_KIND_OPTION:
            usage += parameter.type == COMMAND_ARGUMENT_TYPE_BOOL ? " [--" + parameter.name + "]"
                                                                  : " [--" + parameter.name + " <" + parameter.name + ">]";
            break;
        case COMMAND_PARAMETER_KIND_REMAINING:
            usage += " [" + parameter.name + "...]";
            break;
        case COMMAND_PARAMETER_KIND_COUNT:
            break;
        }
    }
    return usage;
}

static std::string s_describeCommand(const std::string &path, const CommandDescription &command)
{
    std::string text = s_describeUsage(path, command) + "\n  " + command.description + "\n";
    for (const CommandParameter &parameter : command.parameters) {
        text += "  " + parameter.name + " (" + std::string(COMMAND_ARGUMENT_TYPE_NAMES[parameter.type]) + ")  " + parameter.description;
        if (!parameter.choices.empty()) {
            std::string choices;
            for (const std::string &choice : parameter.choices) {
                choices += (choices.empty() ? "" : ", ") + choice;
            }
            text += ", one of " + choices;
        }
        text += "\n";
    }
    return text;
}

static std::string s_describeGroup(const std::string &path, const CommandGroup &group)
{
    std::string text;
    if (!path.empty()) {
        text += path + "\n  " + group.description() + "\n";
    }

    for (const auto &[name, child] : group.groups()) {
        text += "  " + s_joinPath(path, name) + " ...  " + child.description() + "\n";
    }
    for (const auto &[name, command] : group.commands()) {
        text += "  " + s_describeUsage(s_joinPath(path, name), command) + "  " + command.description + "\n";
    }
    return text;
}

bool CommandArguments::has(std::string_view name) const
{
    return m_values.find(name) != m_values.end();
}

std::string_view CommandArguments::string(std::string_view name) const
{
    auto found = m_values.find(name);
    RP_ASSERT(found != m_values.end(), "argument {} was not given", name);
    return std::get<std::string>(found->second);
}

double CommandArguments::number(std::string_view name) const
{
    auto found = m_values.find(name);
    RP_ASSERT(found != m_values.end(), "argument {} was not given", name);
    return std::get<double>(found->second);
}

bool CommandArguments::boolean(std::string_view name) const
{
    auto found = m_values.find(name);
    return found != m_values.end() && std::get<bool>(found->second);
}

void CommandArguments::set(std::string name, CommandArgument value)
{
    m_values.insert_or_assign(std::move(name), std::move(value));
}

CommandReply::~CommandReply()
{
    deliver(COMMAND_RESULT_FAILED, "the command gave no answer");
}

CommandReply::CommandReply(CommandReply &&other) noexcept : m_deliver(std::exchange(other.m_deliver, nullptr)) {}

CommandReply &CommandReply::operator=(CommandReply &&other) noexcept
{
    if (this != &other) {
        deliver(COMMAND_RESULT_FAILED, "the command gave no answer");
        m_deliver = std::exchange(other.m_deliver, nullptr);
    }
    return *this;
}

void CommandReply::succeed(std::string text)
{
    deliver(COMMAND_RESULT_SUCCEEDED, std::move(text));
}

void CommandReply::fail(std::string text)
{
    deliver(COMMAND_RESULT_FAILED, std::move(text));
}

void CommandReply::deliver(CommandResult result, std::string text)
{
    if (m_deliver == nullptr) {
        return;
    }

    Deliver handOver = std::exchange(m_deliver, nullptr);
    handOver(result, std::move(text));
}

CommandGroup &CommandGroup::group(std::string_view name, std::string description)
{
    RP_ASSERT(findCommand(name) == nullptr, "'{}' is already a command, it cannot also be a group", name);

    auto found = m_groups.find(name);
    if (found != m_groups.end()) {
        return found->second;
    }

    RP_ASSERT(!description.empty(), "group '{}' needs a description", name);
    return m_groups.emplace(std::string(name), CommandGroup(std::move(description))).first->second;
}

void CommandGroup::add(CommandDescription command)
{
    RP_ASSERT(findGroup(command.name) == nullptr && findCommand(command.name) == nullptr, "'{}' is already taken in this group",
              command.name);
    RP_ASSERT(!command.name.empty(), "a command needs a name");
    RP_ASSERT(!command.description.empty(), "command '{}' needs a description", command.name);
    RP_ASSERT(command.run != nullptr, "command '{}' runs nothing", command.name);
    for (const CommandParameter &parameter : command.parameters) {
        RP_ASSERT(!parameter.name.empty() && !parameter.description.empty(), "every parameter of '{}' needs a name and a description",
                  command.name);
        RP_ASSERT(parameter.type != COMMAND_ARGUMENT_TYPE_CHOICE || !parameter.choices.empty(), "'{}' of '{}' offers no choices",
                  parameter.name, command.name);
    }

    std::string name = command.name;
    m_commands.emplace(std::move(name), std::move(command));
}

const CommandGroup *CommandGroup::findGroup(std::string_view name) const
{
    auto found = m_groups.find(name);
    return found != m_groups.end() ? &found->second : nullptr;
}

const CommandDescription *CommandGroup::findCommand(std::string_view name) const
{
    auto found = m_commands.find(name);
    return found != m_commands.end() ? &found->second : nullptr;
}

CommandRegistry::CommandRegistry()
{
    add({.name = "help",
         .description = "Describes a group or command, or lists everything at the top level",
         .parameters = {{.name = "path",
                         .kind = COMMAND_PARAMETER_KIND_REMAINING,
                         .description = "The words leading to the group or command"}},
         .run = [this](const CommandArguments &arguments, CommandReply reply) { reply.succeed(describe(arguments.string("path"))); }});

    add({.name = "complete",
         .description = "Lists what the last word of a partly typed line can become, one candidate per line",
         .parameters = {{.name = "line",
                         .kind = COMMAND_PARAMETER_KIND_REMAINING,
                         .description = "The line as typed so far, quoted so a trailing space is kept"}},
         .run = [this](const CommandArguments &arguments, CommandReply reply) {
             std::string listing;
             for (const std::string &candidate : complete(arguments.string("line"))) {
                 listing += candidate + "\n";
             }
             reply.succeed(listing);
         }});
}

void CommandRegistry::execute(std::string_view line, CommandReply reply) const
{
    CommandWords split = s_splitIntoWords(line);
    if (split.hasUnclosedQuote) {
        reply.fail("a quote is never closed");
        return;
    }
    if (split.words.empty()) {
        reply.fail("nothing to run, help lists what there is");
        return;
    }

    std::string error;
    CommandLocation location;
    if (!s_followWords(m_root, split.words, split.words.size(), location, error)) {
        reply.fail(error);
        return;
    }
    if (location.command == nullptr) {
        reply.fail(location.path + " is a group, the choices are: " + s_listGroupContents(*location.group));
        return;
    }

    CommandBinding binding;
    std::span<const CommandWord> argumentWords(split.words.data() + location.argumentStart, split.words.size() - location.argumentStart);
    if (!s_bindWords(*location.command, argumentWords, binding, error) || !s_finishBinding(*location.command, binding, error)) {
        reply.fail(error + "\n" + s_describeUsage(location.path, *location.command));
        return;
    }

    location.command->run(binding.arguments, std::move(reply));
}

std::vector<std::string> CommandRegistry::complete(std::string_view line) const
{
    CommandWords split = s_splitIntoWords(line);

    std::string partial;
    size_t finishedWords = split.words.size();
    if (split.endsInsideWord) {
        partial = split.words.back().text;
        finishedWords--;
    }

    std::string error;
    CommandLocation location;
    if (!s_followWords(m_root, split.words, finishedWords, location, error)) {
        return {};
    }

    std::vector<std::string> candidates;
    if (location.command == nullptr) {
        for (const auto &[name, child] : location.group->groups()) {
            candidates.push_back(name);
        }
        for (const auto &[name, command] : location.group->commands()) {
            candidates.push_back(name);
        }
    } else {
        CommandBinding binding;
        std::span<const CommandWord> argumentWords(split.words.data() + location.argumentStart, finishedWords - location.argumentStart);
        // a word that does not fit leaves the binding at what was read before it, which is all completion needs
        s_bindWords(*location.command, argumentWords, binding, error);
        candidates = s_candidatesForCommand(*location.command, binding, partial);
    }

    std::erase_if(candidates, [&partial](const std::string &candidate) { return !candidate.starts_with(partial); });
    return candidates;
}

std::string CommandRegistry::describe(std::string_view path) const
{
    CommandWords split = s_splitIntoWords(path);

    std::string error;
    CommandLocation location;
    if (!s_followWords(m_root, split.words, split.words.size(), location, error)) {
        return error;
    }

    if (location.command != nullptr) {
        return s_describeCommand(location.path, *location.command);
    }

    return s_describeGroup(location.path, *location.group);
}

} // namespace Rapture
