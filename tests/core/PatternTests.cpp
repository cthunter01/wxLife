#include "wxLife/core/Pattern.h"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "support/AsciiGrid.h"
#include "wxLife/core/Grid.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"

namespace wxLife::core
{
namespace
{

using test::toAscii;

// The pattern's cells drawn on a grid of its extent, for comparing with text art.
std::string ascii(const Pattern& pattern)
{
    Grid grid(pattern.extent);
    for (const CellPos cell : pattern.cells)
    {
        grid.set(cell, kAlive);
    }
    return toAscii(grid);
}

Pattern read(std::string_view text)
{
    const std::expected<Pattern, PatternError> pattern = readPattern(text);
    if (!pattern)
    {
        ADD_FAILURE() << "readPattern failed: " << describe(pattern.error());
        return {};
    }
    return *pattern;
}

PatternError failure(std::string_view text)
{
    const std::expected<Pattern, PatternError> pattern = readPattern(text);
    if (pattern)
    {
        ADD_FAILURE() << "readPattern accepted:\n" << text;
        return {};
    }
    return pattern.error();
}

std::string ruleText(const Pattern& pattern)
{
    return pattern.rule ? pattern.rule->toString() : "none";
}

TEST(PatternTest, ReadsAnRleFileWithItsComments)
{
    const Pattern glider = read(
        "#N Glider\n"
        "#O Richard K. Guy\n"
        "#C The smallest spaceship.\n"
        "#c www.conwaylife.com/wiki/Glider\n"
        "x = 3, y = 3, rule = B3/S23\n"
        "bo$2bo$3o!\n");
    EXPECT_EQ(glider.name, "Glider");
    EXPECT_EQ(glider.author, "Richard K. Guy");
    EXPECT_EQ(glider.comments, (std::vector<std::string>{"The smallest spaceship.",
                                                         "www.conwaylife.com/wiki/Glider"}));
    EXPECT_EQ(ruleText(glider), "B3/S23");
    EXPECT_EQ(glider.extent, (Extent{3, 3}));
    EXPECT_EQ(ascii(glider),
              ".O.\n"
              "..O\n"
              "OOO\n");
    // Row by row, each row from the left.
    EXPECT_EQ(glider.cells, (std::vector<CellPos>{{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}));
}

TEST(PatternTest, RleRunsMayBeSplitAnywhereBetweenRuns)
{
    // CRLF line ends, white space between runs and between a count and its tag, a row broken over
    // two lines, a comment line in the body, several rows skipped at once, and a missing '!'.
    const Pattern pattern = read(
        "x = 5, y = 4\r\n"
        "2o 3\tb$\r\n"
        "#C a comment in the body\r\n"
        "o3b\r\n"
        "o2$\r\n"
        "2 o b 2o\r\n");
    EXPECT_EQ(ascii(pattern),
              "OO...\n"
              "O...O\n"
              ".....\n"
              "OO.OO\n");
    EXPECT_FALSE(pattern.rule.has_value());

    // Text after '!' is ignored, and so are the header's unknown keys.
    EXPECT_EQ(ascii(read("x = 2, y = 1, z = 7\n2o!bo$garbage")), "OO\n");
    // Multi-state RLE writes a two-state pattern with '.' and 'A'.
    EXPECT_EQ(ascii(read("x = 3, y = 1, rule = B3/S23\nA.A!")), "O.O\n");
    // Keys and letters in any case, and no spaces at all.
    EXPECT_EQ(ascii(read("X=2,Y=2,RULE=b3/s23\nbo$o!")), ".O\nO.\n");
}

TEST(PatternTest, TheExtentIsTheHeaderSizeGrownToTheCells)
{
    // Room the header asks for is kept; cells outside it widen the pattern.
    EXPECT_EQ(read("x = 5, y = 3\no!").extent, (Extent{5, 3}));
    EXPECT_EQ(read("x = 1, y = 1\n3o$$bo!").extent, (Extent{3, 3}));
    EXPECT_EQ(read("x = 0, y = 0\n!").extent, (Extent{0, 0}));
    EXPECT_TRUE(read("x = 0, y = 0\n!").cells.empty());
}

TEST(PatternTest, ReadsEveryRuleSpelling)
{
    EXPECT_EQ(ruleText(read("x = 1, y = 1, rule = b36/s23\no!")), "B36/S23");
    EXPECT_EQ(ruleText(read("x = 1, y = 1, rule = 23/3\no!")), "B3/S23");
    EXPECT_EQ(ruleText(read("x = 1, y = 1, rule = s23/b3\no!")), "B3/S23");
    EXPECT_EQ(ruleText(read("x = 1, y = 1, rule = Life\no!")), "B3/S23");
    EXPECT_EQ(ruleText(read("x = 1, y = 1, rule = highlife\no!")), "B36/S23");  // a preset name
    // Golly's bounded grid suffix, which contains a comma of its own.
    EXPECT_EQ(ruleText(read("x = 1, y = 1, rule = B3/S23:T20,30\no!")), "B3/S23");
    EXPECT_EQ(ruleText(read("x = 1, y = 1, rule =\no!")), "none");
    // The old "#r" line.
    EXPECT_EQ(ruleText(read("#r 23/36\nx = 1, y = 1\no!")), "B36/S23");
}

TEST(PatternTest, ReadsPlaintext)
{
    const Pattern glider = read(
        "!Name: Glider\r\n"
        "!Author: Richard K. Guy\r\n"
        "!The smallest spaceship.\r\n"
        ".O\r\n"
        "..O\r\n"
        "OOO\r\n"
        "\r\n");
    EXPECT_EQ(glider.name, "Glider");
    EXPECT_EQ(glider.author, "Richard K. Guy");
    EXPECT_EQ(glider.comments, (std::vector<std::string>{"The smallest spaceship."}));
    EXPECT_FALSE(glider.rule.has_value());
    // The longest row sets the width; the empty line at the end is no row.
    EXPECT_EQ(ascii(glider),
              ".O.\n"
              "..O\n"
              "OOO\n");

    // An empty line between rows is an empty row, '*' is alive too, and trailing blanks are
    // ignored.
    EXPECT_EQ(ascii(read("*.  \n\n.*\n")), "O.\n..\n.O\n");
    EXPECT_EQ(read("!Only a comment\n").extent, (Extent{0, 0}));
}

TEST(PatternTest, RejectsWhatItCannotRead)
{
    using enum PatternErrorKind;
    const auto expect = [](std::string_view text, PatternErrorKind kind, int line,
                           std::string_view detail = {}) {
        const PatternError error = failure(text);
        EXPECT_EQ(error.kind, kind) << text;
        EXPECT_EQ(error.line, line) << text;
        EXPECT_EQ(error.detail, detail) << text;
    };
    expect("", EMPTY, 0);
    expect(" \n\t\r\n", EMPTY, 0);
    expect("hello", UNKNOWN_FORMAT, 0);
    expect("[M2] (golly 4.2)\n#R B3/S23\n", UNSUPPORTED_FORMAT, 0, "macrocell (.mc)");
    expect("#Life 1.06\n0 0\n", UNSUPPORTED_FORMAT, 0, "Life 1.06");
    expect("#N Glider\nbo$2bo$3o!", NO_HEADER, 2);
    expect("#N Only comments\n", NO_HEADER, 2);
    expect("x = 3\nbo$2bo$3o!", BAD_HEADER, 1, "x = 3");
    expect("x = -3, y = 3\n", BAD_HEADER, 1, "x = -3, y = 3");
    expect("x = 3, y = three\n", BAD_HEADER, 1, "x = 3, y = three");
    expect("x = 3, y = 3, rule = LifeHistory\n", UNSUPPORTED_RULE, 1, "LifeHistory");
    expect("x = 3, y = 3, rule = B3/S23V\n", UNSUPPORTED_RULE, 1, "B3/S23V");
    expect("#r Wireworld\nx = 1, y = 1\n", UNSUPPORTED_RULE, 1, "Wireworld");
    expect("x = 3, y = 1\n\nAB!", MULTI_STATE, 3);
    expect("x = 3, y = 1\nb%o!", BAD_CHARACTER, 2, "'%'");
    expect("x = 3, y = 1\nbqo!", MULTI_STATE, 2);  // p to y prefix states above 24
    expect("x = 3, y = 1\nb\xC3\xA9o!", BAD_CHARACTER, 2, "(byte 0xC3)");
    expect(".O.\n.X.\n", BAD_CHARACTER, 2, "'X'");
}

TEST(PatternTest, RejectsPatternsLargerThanAWorld)
{
    using enum PatternErrorKind;
    // The Caterpillar spaceship: its header alone is enough to refuse it.
    const PatternError error = failure("#N Caterpillar\nx = 4195, y = 330721, rule = B3/S23\n");
    EXPECT_EQ(error.kind, TOO_LARGE);
    EXPECT_EQ(error.detail, "4,195 × 330,721");
    EXPECT_EQ(describe(error),
              "Line 2: The pattern is 4,195 × 330,721 cells; a world has at most 100,000 cells "
              "per side.");

    // Runs that reach beyond the largest world, whatever the header says.
    const std::string side = std::to_string(kMaxWorldSide);
    EXPECT_EQ(failure("x = 1, y = 1\n" + side + "0b!").kind, TOO_LARGE);
    EXPECT_EQ(failure("x = 1, y = 1\n" + side + "bo!").kind, TOO_LARGE);
    EXPECT_EQ(failure("x = 1, y = 1\n" + side + "$o!").kind, TOO_LARGE);
    // Right up to the limit is fine.
    const Pattern widest = read("x = 1, y = 1\n" + std::to_string(kMaxWorldSide - 1) + "bo!");
    EXPECT_EQ(widest.extent, (Extent{kMaxWorldSide, 1}));
    EXPECT_EQ(widest.cells, (std::vector<CellPos>{{kMaxWorldSide - 1, 0}}));
}

TEST(PatternTest, DescribesEachError)
{
    using enum PatternErrorKind;
    EXPECT_EQ(describe({.kind = EMPTY, .line = 0, .detail = ""}), "The file is empty.");
    EXPECT_EQ(describe({.kind = UNSUPPORTED_FORMAT, .line = 0, .detail = "macrocell (.mc)"}),
              "wxLife reads RLE (.rle) and plaintext (.cells) patterns, not macrocell (.mc) "
              "files.");
    EXPECT_EQ(describe({.kind = BAD_CHARACTER, .line = 7, .detail = "'q'"}),
              "Line 7: Unexpected character 'q'.");
    EXPECT_EQ(describe({.kind = UNSUPPORTED_RULE, .line = 1, .detail = "LifeHistory"}),
              "Line 1: wxLife runs two-state B/S rules such as B3/S23, not \"LifeHistory\".");
    EXPECT_EQ(describe({.kind = TOO_LARGE, .line = 3, .detail = ""}),
              "Line 3: The pattern is wider or taller than 100,000 cells.");
}

}  // namespace
}  // namespace wxLife::core
