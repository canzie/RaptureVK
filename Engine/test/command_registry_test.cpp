#include <gtest/gtest.h>

#include <rapture/core/commands/CommandRegistry.h>

#include <string>

using Rapture::CommandArguments;
using Rapture::CommandCall;
using Rapture::CommandOutcome;
using Rapture::CommandReply;

struct RecordedAnswer {
    bool answered = false;
    CommandOutcome outcome = Rapture::COMMAND_FAILED;
    std::string text;
};

static CommandReply s_recordInto(RecordedAnswer &answer)
{
    return CommandReply([&answer](CommandOutcome outcome, std::string text) {
        answer.answered = true;
        answer.outcome = outcome;
        answer.text = std::move(text);
    });
}

TEST(CommandCallParse, ReadsNameAndEveryArgumentType)
{
    CommandCall call;
    std::string error;
    ASSERT_TRUE(Rapture::CommandCall_parse(R"( screenshot.panel( "Out \"liner\"" , -2.5, true ) )", call, error)) << error;

    EXPECT_EQ(call.name, "screenshot.panel");
    ASSERT_EQ(call.arguments.size(), 3u);
    EXPECT_EQ(std::get<std::string>(call.arguments[0]), "Out \"liner\"");
    EXPECT_DOUBLE_EQ(std::get<double>(call.arguments[1]), -2.5);
    EXPECT_TRUE(std::get<bool>(call.arguments[2]));
}

TEST(CommandCallParse, ReadsACallWithoutArguments)
{
    CommandCall call;
    std::string error;
    ASSERT_TRUE(Rapture::CommandCall_parse("workspace.list()", call, error)) << error;

    EXPECT_EQ(call.name, "workspace.list");
    EXPECT_TRUE(call.arguments.empty());
}

TEST(CommandCallParse, RejectsMalformedText)
{
    CommandCall call;
    std::string error;
    EXPECT_FALSE(Rapture::CommandCall_parse("workspace.list", call, error));
    EXPECT_FALSE(Rapture::CommandCall_parse("workspace..list()", call, error));
    EXPECT_FALSE(Rapture::CommandCall_parse("a(\"never closed)", call, error));
    EXPECT_FALSE(Rapture::CommandCall_parse("a(1 2)", call, error));
    EXPECT_FALSE(Rapture::CommandCall_parse("a(word)", call, error));
    EXPECT_FALSE(Rapture::CommandCall_parse("a() trailing", call, error));
}

TEST(CommandRegistry, RunsACommandWithItsArguments)
{
    Rapture::CommandRegistry registry;
    registry.add("math.add", "Adds two numbers", {{"a", Rapture::COMMAND_ARGUMENT_NUMBER}, {"b", Rapture::COMMAND_ARGUMENT_NUMBER}},
                 [](const CommandArguments &arguments, CommandReply reply) {
                     reply.succeed(std::to_string(static_cast<int>(arguments.number(0) + arguments.number(1))));
                 });

    RecordedAnswer answer;
    registry.execute("math.add(2, 3)", s_recordInto(answer));

    ASSERT_TRUE(answer.answered);
    EXPECT_EQ(answer.outcome, Rapture::COMMAND_SUCCEEDED);
    EXPECT_EQ(answer.text, "5");
}

TEST(CommandRegistry, FailsUnknownCommandsWrongArityAndWrongTypes)
{
    Rapture::CommandRegistry registry;
    registry.add("echo", "Echoes text", {{"text", Rapture::COMMAND_ARGUMENT_STRING}},
                 [](const CommandArguments &arguments, CommandReply reply) { reply.succeed(std::string(arguments.string(0))); });

    RecordedAnswer unknown;
    registry.execute("missing()", s_recordInto(unknown));
    EXPECT_EQ(unknown.outcome, Rapture::COMMAND_FAILED);

    RecordedAnswer arity;
    registry.execute("echo()", s_recordInto(arity));
    EXPECT_EQ(arity.outcome, Rapture::COMMAND_FAILED);

    RecordedAnswer type;
    registry.execute("echo(3)", s_recordInto(type));
    EXPECT_EQ(type.outcome, Rapture::COMMAND_FAILED);
}

TEST(CommandRegistry, AReplyNeverAnsweredFailsOnce)
{
    Rapture::CommandRegistry registry;
    registry.add("silent", "Answers nothing", {}, [](const CommandArguments &arguments, CommandReply reply) {
        (void)arguments;
        (void)reply;
    });

    int answers = 0;
    CommandOutcome outcome = Rapture::COMMAND_SUCCEEDED;
    registry.execute("silent()", CommandReply([&](CommandOutcome given, std::string text) {
                         (void)text;
                         answers++;
                         outcome = given;
                     }));

    EXPECT_EQ(answers, 1);
    EXPECT_EQ(outcome, Rapture::COMMAND_FAILED);
}

TEST(CommandRegistry, HelpListsRegisteredCommands)
{
    Rapture::CommandRegistry registry;
    registry.add("workspace.list", "Lists workspaces", {}, [](const CommandArguments &arguments, CommandReply reply) {
        (void)arguments;
        reply.succeed();
    });

    RecordedAnswer answer;
    registry.execute("help()", s_recordInto(answer));

    EXPECT_EQ(answer.outcome, Rapture::COMMAND_SUCCEEDED);
    EXPECT_NE(answer.text.find("workspace.list()  Lists workspaces"), std::string::npos);
}
