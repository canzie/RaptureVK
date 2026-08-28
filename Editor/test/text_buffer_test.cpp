#include "text/TextBuffer.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using Amethyst::TextPosition;
using Amethyst::TextRange;

namespace {

TextRange at(uint64_t line, uint64_t column)
{
    return TextRange{TextPosition{line, column}, TextPosition{line, column}};
}

TextRange span(uint64_t startLine, uint64_t startColumn, uint64_t endLine, uint64_t endColumn)
{
    return TextRange{TextPosition{startLine, startColumn}, TextPosition{endLine, endColumn}};
}

std::vector<std::string> allLines(const TextBuffer &buffer)
{
    std::vector<std::string> out;
    for (uint64_t i = 0; i < buffer.lineCount(); i++) {
        out.emplace_back(buffer.line(i));
    }
    return out;
}

// Enough distinct lines to span many blocks, so block boundaries land mid-test.
std::string manyLines(size_t count)
{
    std::string out;
    for (size_t i = 0; i < count; i++) {
        out += "line " + std::to_string(i);
        if (i + 1 < count) {
            out.push_back('\n');
        }
    }
    return out;
}

} // namespace

TEST(TextBuffer, EmptyBufferHasOneEmptyLine)
{
    TextBuffer buffer;
    EXPECT_EQ(buffer.lineCount(), 1u);
    EXPECT_EQ(buffer.line(0), "");
    EXPECT_EQ(buffer.text(), "");
}

TEST(TextBuffer, SplitsOnNewlines)
{
    TextBuffer buffer("alpha\nbeta\ngamma");
    ASSERT_EQ(buffer.lineCount(), 3u);
    EXPECT_EQ(buffer.line(0), "alpha");
    EXPECT_EQ(buffer.line(1), "beta");
    EXPECT_EQ(buffer.line(2), "gamma");
}

TEST(TextBuffer, TrailingNewlineLeavesAnEmptyFinalLine)
{
    TextBuffer buffer("alpha\n");
    ASSERT_EQ(buffer.lineCount(), 2u);
    EXPECT_EQ(buffer.line(1), "");
}

TEST(TextBuffer, LineOutOfRangeIsEmpty)
{
    TextBuffer buffer("alpha");
    EXPECT_EQ(buffer.line(1), "");
    EXPECT_EQ(buffer.line(99999), "");
}

TEST(TextBuffer, RoundTripsLf)
{
    std::string source = "alpha\nbeta\ngamma";
    TextBuffer buffer(source);
    EXPECT_EQ(buffer.lineEnding(), LineEnding::LF);
    EXPECT_EQ(buffer.text(), source);
}

TEST(TextBuffer, RoundTripsCrlf)
{
    std::string source = "alpha\r\nbeta\r\ngamma";
    TextBuffer buffer(source);
    EXPECT_EQ(buffer.lineEnding(), LineEnding::CRLF);
    EXPECT_EQ(buffer.line(0), "alpha");
    EXPECT_EQ(buffer.text(), source);
}

TEST(TextBuffer, AFewStrayCarriageReturnsStayLf)
{
    TextBuffer buffer("a\nb\nc\nd\ne\nf\ng\r\nh");
    EXPECT_EQ(buffer.lineEnding(), LineEnding::LF);
}

TEST(TextBuffer, InsertWithinALine)
{
    TextBuffer buffer("alpha\nbeta");
    buffer.replace(at(0, 2), "XY");
    EXPECT_EQ(buffer.line(0), "alXYpha");
    EXPECT_EQ(buffer.lineCount(), 2u);
}

TEST(TextBuffer, InsertingANewlineSplitsTheLine)
{
    TextBuffer buffer("alpha\nbeta");
    buffer.replace(at(0, 2), "\n");
    ASSERT_EQ(buffer.lineCount(), 3u);
    EXPECT_EQ(buffer.line(0), "al");
    EXPECT_EQ(buffer.line(1), "pha");
    EXPECT_EQ(buffer.line(2), "beta");
}

TEST(TextBuffer, InsertingMultipleLines)
{
    TextBuffer buffer("alpha");
    buffer.replace(at(0, 5), "\none\ntwo");
    ASSERT_EQ(buffer.lineCount(), 3u);
    EXPECT_EQ(buffer.line(0), "alpha");
    EXPECT_EQ(buffer.line(1), "one");
    EXPECT_EQ(buffer.line(2), "two");
}

TEST(TextBuffer, ErasingWithinALine)
{
    TextBuffer buffer("alpha\nbeta");
    buffer.replace(span(0, 1, 0, 4), "");
    EXPECT_EQ(buffer.line(0), "aa");
    EXPECT_EQ(buffer.lineCount(), 2u);
}

TEST(TextBuffer, ErasingAcrossLinesJoinsThem)
{
    TextBuffer buffer("alpha\nbeta\ngamma");
    buffer.replace(span(0, 2, 2, 2), "");
    ASSERT_EQ(buffer.lineCount(), 1u);
    EXPECT_EQ(buffer.line(0), "almma");
}

TEST(TextBuffer, ReplacingAcrossLines)
{
    TextBuffer buffer("alpha\nbeta\ngamma");
    buffer.replace(span(0, 2, 2, 2), "-\n-");
    ASSERT_EQ(buffer.lineCount(), 2u);
    EXPECT_EQ(buffer.line(0), "al-");
    EXPECT_EQ(buffer.line(1), "-mma");
}

TEST(TextBuffer, ColumnsPastTheEndClampToIt)
{
    TextBuffer buffer("alpha");
    buffer.replace(at(0, 9999), "!");
    EXPECT_EQ(buffer.line(0), "alpha!");
}

TEST(TextBuffer, RevisionMovesOnEveryEdit)
{
    TextBuffer buffer("alpha");
    uint64_t before = buffer.revision();
    buffer.replace(at(0, 0), "x");
    EXPECT_NE(buffer.revision(), before);
}

TEST(TextBuffer, ReadsBackEveryLineAcrossManyBlocks)
{
    constexpr size_t COUNT = 20000;
    TextBuffer buffer(manyLines(COUNT));
    ASSERT_EQ(buffer.lineCount(), COUNT);
    for (size_t i = 0; i < COUNT; i++) {
        EXPECT_EQ(buffer.line(i), "line " + std::to_string(i)) << "at line " << i;
    }
}

TEST(TextBuffer, RandomAccessMatchesSequential)
{
    constexpr size_t COUNT = 5000;
    TextBuffer buffer(manyLines(COUNT));
    for (size_t i = COUNT; i-- > 0;) {
        ASSERT_EQ(buffer.line(i), "line " + std::to_string(i)) << "at line " << i;
    }
}

TEST(TextBuffer, EditInTheMiddleOfManyBlocksKeepsEverythingElse)
{
    constexpr size_t COUNT = 20000;
    TextBuffer buffer(manyLines(COUNT));
    buffer.replace(at(COUNT / 2, 0), "EDIT ");

    ASSERT_EQ(buffer.lineCount(), COUNT);
    EXPECT_EQ(buffer.line(COUNT / 2), "EDIT line " + std::to_string(COUNT / 2));
    EXPECT_EQ(buffer.line(0), "line 0");
    EXPECT_EQ(buffer.line(COUNT - 1), "line " + std::to_string(COUNT - 1));
    EXPECT_EQ(buffer.line(COUNT / 2 - 1), "line " + std::to_string(COUNT / 2 - 1));
    EXPECT_EQ(buffer.line(COUNT / 2 + 1), "line " + std::to_string(COUNT / 2 + 1));
}

TEST(TextBuffer, InsertingLinesInTheMiddleKeepsOrder)
{
    constexpr size_t COUNT = 5000;
    TextBuffer buffer(manyLines(COUNT));
    buffer.replace(at(COUNT / 2, 0), "one\ntwo\n");

    ASSERT_EQ(buffer.lineCount(), COUNT + 2);
    EXPECT_EQ(buffer.line(COUNT / 2), "one");
    EXPECT_EQ(buffer.line(COUNT / 2 + 1), "two");
    EXPECT_EQ(buffer.line(COUNT / 2 + 2), "line " + std::to_string(COUNT / 2));
    EXPECT_EQ(buffer.line(COUNT + 1), "line " + std::to_string(COUNT - 1));
}

TEST(TextBuffer, ErasingManyLinesKeepsOrder)
{
    constexpr size_t COUNT = 5000;
    TextBuffer buffer(manyLines(COUNT));
    buffer.replace(span(1000, 0, 2000, 0), "");

    ASSERT_EQ(buffer.lineCount(), COUNT - 1000);
    EXPECT_EQ(buffer.line(999), "line 999");
    EXPECT_EQ(buffer.line(1000), "line 2000");
    EXPECT_EQ(buffer.line(COUNT - 1001), "line " + std::to_string(COUNT - 1));
}

TEST(TextBuffer, ALineLongerThanABlockSurvives)
{
    std::string huge(TextBuffer::BLOCK_BYTES * 3, 'x');
    TextBuffer buffer("before\n" + huge + "\nafter");

    ASSERT_EQ(buffer.lineCount(), 3u);
    EXPECT_EQ(buffer.line(0), "before");
    EXPECT_EQ(buffer.line(1).size(), huge.size());
    EXPECT_EQ(buffer.line(1), huge);
    EXPECT_EQ(buffer.line(2), "after");
}

TEST(TextBuffer, GrowingALinePastABlockSplitsIt)
{
    TextBuffer buffer(manyLines(500));
    std::string padding(TextBuffer::BLOCK_BYTES * 2, 'y');
    buffer.replace(at(100, 0), padding);

    ASSERT_EQ(buffer.lineCount(), 500u);
    EXPECT_EQ(buffer.line(100), padding + "line 100");
    EXPECT_EQ(buffer.line(99), "line 99");
    EXPECT_EQ(buffer.line(101), "line 101");
    EXPECT_EQ(buffer.line(499), "line 499");
}

TEST(TextBuffer, TextMatchesAfterEditing)
{
    TextBuffer buffer("alpha\nbeta\ngamma");
    buffer.replace(at(1, 4), "!");
    EXPECT_EQ(buffer.text(), "alpha\nbeta!\ngamma");
}
