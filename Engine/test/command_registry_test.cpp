#include <gtest/gtest.h>

#include <rapture/core/commands/CommandRegistry.h>

#include <algorithm>
#include <string>
#include <vector>

using Rapture::CommandArguments;
using Rapture::CommandReply;
using Rapture::CommandResult;

struct RecordedAnswer {
    int answers = 0;
    CommandResult result = Rapture::COMMAND_RESULT_FAILED;
    std::string text;
};

static CommandReply s_recordInto(RecordedAnswer &answer)
{
    return CommandReply([&answer](CommandResult result, std::string text) {
        answer.answers++;
        answer.result = result;
        answer.text = std::move(text);
    });
}

static bool s_contains(const std::vector<Rapture::CommandCompletion> &candidates, std::string_view text)
{
    return std::any_of(candidates.begin(), candidates.end(),
                       [text](const Rapture::CommandCompletion &candidate) { return candidate.text == text; });
}

/**
 * @brief A registry holding one screenshot panel command that echoes what it was given
 */
class CommandRegistryTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        Rapture::CommandGroup &screenshot = registry.group("screenshot", "Captures");
        screenshot.add({.name = "panel",
                        .description = "Captures a panel",
                        .parameters = {{.name = "name",
                                        .kind = Rapture::COMMAND_PARAMETER_KIND_REMAINING,
                                        .description = "The panel",
                                        .complete =
                                            [](const CommandArguments &given) {
                                                if (given.has("workspace") && given.string("workspace") == "Scripting") {
                                                    return std::vector<std::string>{"Text Editor"};
                                                }
                                                return std::vector<std::string>{"Outliner", "Viewport"};
                                            }},
                                       {.name = "id",
                                        .type = Rapture::COMMAND_ARGUMENT_TYPE_NUMBER,
                                        .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
                                        .description = "The panel's id"},
                                       {.name = "workspace", .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION, .description = "Where"},
                                       {.name = "format",
                                        .type = Rapture::COMMAND_ARGUMENT_TYPE_CHOICE,
                                        .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
                                        .description = "How",
                                        .choices = {"png", "bmp"}},
                                       {.name = "hidden",
                                        .type = Rapture::COMMAND_ARGUMENT_TYPE_BOOL,
                                        .kind = Rapture::COMMAND_PARAMETER_KIND_OPTION,
                                        .description = "Whether"}},
                        .run = [](const CommandArguments &arguments, CommandReply reply) {
                            std::string text = std::string(arguments.string("name"));
                            if (arguments.has("id")) {
                                text += " id=" + std::to_string(static_cast<int>(arguments.number("id")));
                            }
                            if (arguments.has("workspace")) {
                                text += " workspace=" + std::string(arguments.string("workspace"));
                            }
                            if (arguments.has("format")) {
                                text += " format=" + std::string(arguments.string("format"));
                            }
                            text += arguments.boolean("hidden") ? " hidden" : "";
                            reply.succeed(text);
                        }});

        registry.group("math", "Arithmetic")
            .add({.name = "add",
                  .description = "Adds two numbers",
                  .parameters = {{.name = "a", .type = Rapture::COMMAND_ARGUMENT_TYPE_NUMBER, .description = "First"},
                                 {.name = "b", .type = Rapture::COMMAND_ARGUMENT_TYPE_NUMBER, .description = "Second"}},
                  .run = [](const CommandArguments &arguments, CommandReply reply) {
                      reply.succeed(std::to_string(static_cast<int>(arguments.number("a") + arguments.number("b"))));
                  }});
    }

    RecordedAnswer run(std::string_view line)
    {
        RecordedAnswer answer;
        registry.execute(line, s_recordInto(answer));
        return answer;
    }

    Rapture::CommandRegistry registry;
};

TEST_F(CommandRegistryTest, RunsAPositionalCommandInAGroup)
{
    RecordedAnswer answer = run("math add 2 3");
    EXPECT_EQ(answer.result, Rapture::COMMAND_RESULT_SUCCEEDED);
    EXPECT_EQ(answer.text, "5");
}

TEST_F(CommandRegistryTest, ReadsRemainingWordsQuotesAndOptions)
{
    RecordedAnswer answer = run(R"(screenshot panel Asset Details --workspace "World Editor" --id 4 --format png --hidden)");
    EXPECT_EQ(answer.result, Rapture::COMMAND_RESULT_SUCCEEDED) << answer.text;
    EXPECT_EQ(answer.text, "Asset Details id=4 workspace=World Editor format=png hidden");
}

TEST_F(CommandRegistryTest, AQuotedWordIsNeverAnOption)
{
    RecordedAnswer answer = run(R"(screenshot panel "--id")");
    EXPECT_EQ(answer.result, Rapture::COMMAND_RESULT_SUCCEEDED) << answer.text;
    EXPECT_EQ(answer.text, "--id");
}

TEST_F(CommandRegistryTest, FailsWhatDoesNotFit)
{
    EXPECT_EQ(run("").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("missing").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("math").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("math add 2").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("math add 2 3 4").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("math add two 3").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("screenshot panel --format gif").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("screenshot panel --id").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("screenshot panel --id 1 --id 2").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("screenshot panel --nope").result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_EQ(run("screenshot panel \"never closed").result, Rapture::COMMAND_RESULT_FAILED);
}

TEST_F(CommandRegistryTest, NamesTheChoicesWhenAWordNamesNothing)
{
    RecordedAnswer answer = run("screenshot nope");
    EXPECT_EQ(answer.result, Rapture::COMMAND_RESULT_FAILED);
    EXPECT_NE(answer.text.find("panel"), std::string::npos);
}

TEST_F(CommandRegistryTest, AReplyNeverAnsweredFailsOnce)
{
    registry.add({.name = "silent", .description = "Answers nothing", .run = [](const CommandArguments &arguments, CommandReply reply) {
                      (void)arguments;
                      (void)reply;
                  }});

    RecordedAnswer answer = run("silent");
    EXPECT_EQ(answer.answers, 1);
    EXPECT_EQ(answer.result, Rapture::COMMAND_RESULT_FAILED);
}

TEST_F(CommandRegistryTest, CompletesGroupsCommandsOptionsAndValues)
{
    EXPECT_TRUE(s_contains(registry.complete("scr"), "screenshot"));
    EXPECT_TRUE(s_contains(registry.complete("screenshot "), "panel"));
    EXPECT_TRUE(s_contains(registry.complete("screenshot panel --f"), "--format"));
    EXPECT_FALSE(s_contains(registry.complete("screenshot panel --id 3 --"), "--id"));
    EXPECT_TRUE(s_contains(registry.complete("screenshot panel --format "), "bmp"));
    EXPECT_TRUE(s_contains(registry.complete("screenshot panel Out"), "Outliner"));
    EXPECT_FALSE(s_contains(registry.complete("screenshot panel Out"), "Viewport"));
}

TEST_F(CommandRegistryTest, CompletersSeeWhatTheCallAlreadyHolds)
{
    std::vector<Rapture::CommandCompletion> candidates = registry.complete("screenshot panel --workspace Scripting ");
    EXPECT_TRUE(s_contains(candidates, "Text Editor"));
    EXPECT_FALSE(s_contains(candidates, "Outliner"));
}

TEST_F(CommandRegistryTest, HelpDescribesGroupsAndCommands)
{
    RecordedAnswer top = run("help");
    EXPECT_NE(top.text.find("screenshot ..."), std::string::npos);

    RecordedAnswer command = run("help screenshot panel");
    EXPECT_NE(command.text.find("[--format <format>]"), std::string::npos);
    EXPECT_NE(command.text.find("one of png, bmp"), std::string::npos);
}
