#include "wxLife/core/Rule.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "wxLife/core/Random.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{
namespace
{

// The parser is constexpr, so most of its contract is checked at compile time.
constexpr bool parsesTo(std::string_view text, Rule::Mask birth, Rule::Mask survival)
{
    const auto rule = Rule::parse(text);
    return rule && rule->birthMask() == birth && rule->survivalMask() == survival;
}

constexpr bool failsWith(std::string_view text, RuleError error)
{
    const auto rule = Rule::parse(text);
    return !rule && rule.error() == error;
}

static_assert(Rule{} == Rule::parse("B3/S23").value());
static_assert(parsesTo("B3/S23", 0b1000, 0b1100));
static_assert(parsesTo("b36/s23", 0b100'1000, 0b1100));
static_assert(parsesTo("S23/B3", 0b1000, 0b1100));
static_assert(parsesTo("B3S23", 0b1000, 0b1100));
static_assert(parsesTo("B/S", 0, 0));
static_assert(parsesTo("B3", 0b1000, 0));
static_assert(parsesTo(" 23/3 ", 0b1000, 0b1100));  // legacy survival/birth
static_assert(parsesTo("\t23/3\n", 0b1000, 0b1100));
static_assert(parsesTo("/3", 0b1000, 0));
static_assert(parsesTo("B012345678/S012345678", 0x1FF, 0x1FF));
static_assert(parsesTo("B0/S8", 0b1, 0b1'0000'0000));

static_assert(failsWith("", RuleError::EMPTY));
static_assert(failsWith(" \t ", RuleError::EMPTY));
static_assert(failsWith("B9", RuleError::NEIGHBOUR_OUT_OF_RANGE));
static_assert(failsWith("23/9", RuleError::NEIGHBOUR_OUT_OF_RANGE));
static_assert(failsWith("B3/B4", RuleError::SYNTAX));
static_assert(failsWith("B3/S23/", RuleError::SYNTAX));
static_assert(failsWith("X3/S2", RuleError::SYNTAX));
static_assert(failsWith("B3/", RuleError::SYNTAX));
static_assert(failsWith("B3 /S23", RuleError::SYNTAX));
static_assert(failsWith("23", RuleError::SYNTAX));  // the legacy form needs its slash
static_assert(failsWith("23/3/", RuleError::SYNTAX));

static_assert(Rule{0xFFFF, 0xFFFF} == Rule{0x1FF, 0x1FF});
static_assert(kRulePresets[findPreset(Rule{}).value()].name == "Conway's Life");
static_assert(findPreset(kRulePresets.back().rule) == kRulePresets.size() - 1);
static_assert(!findPreset(Rule::parse("B0/S8").value()));

// The canonical text of the parsed rule, or the error message, so that failures read well.
std::string parsed(std::string_view text)
{
    const auto rule = Rule::parse(text);
    return rule ? rule->toString() : std::string(describe(rule.error()));
}

TEST(RuleTest, AcceptsTheCommonForms)
{
    EXPECT_EQ(parsed("B3/S23"), "B3/S23");
    EXPECT_EQ(parsed("b36/s23"), "B36/S23");
    EXPECT_EQ(parsed("S23/B3"), "B3/S23");
    EXPECT_EQ(parsed("B3S23"), "B3/S23");
    EXPECT_EQ(parsed("B/S"), "B/S");
    EXPECT_EQ(parsed(" 23/3 "), "B3/S23");
    EXPECT_EQ(parsed("23/36"), "B36/S23");
}

TEST(RuleTest, ReportsEachError)
{
    EXPECT_EQ(parsed(""), describe(RuleError::EMPTY));
    EXPECT_EQ(parsed("   "), describe(RuleError::EMPTY));
    EXPECT_EQ(parsed("B9"), describe(RuleError::NEIGHBOUR_OUT_OF_RANGE));
    EXPECT_EQ(parsed("B3/B4"), describe(RuleError::SYNTAX));
    EXPECT_EQ(parsed("B3/S23/"), describe(RuleError::SYNTAX));
    EXPECT_EQ(parsed("X3/S2"), describe(RuleError::SYNTAX));
}

TEST(RuleTest, DescribesEachError)
{
    EXPECT_EQ(describe(RuleError::EMPTY), "Enter a rule such as B3/S23.");
    EXPECT_EQ(describe(RuleError::SYNTAX), "Rules look like B3/S23: digits after B and S.");
    EXPECT_EQ(describe(RuleError::NEIGHBOUR_OUT_OF_RANGE), "Neighbour counts go from 0 to 8.");
}

TEST(RuleTest, ToStringIsCanonical)
{
    EXPECT_EQ(Rule{}.toString(), "B3/S23");
    EXPECT_EQ(parsed("s32/B63"), "B36/S23");
    EXPECT_EQ((Rule{0b100, 0}).toString(), "B2/S");
    EXPECT_EQ((Rule{0, 0}).toString(), "B/S");
    EXPECT_EQ((Rule{0x1FF, 0x1FF}).toString(), "B012345678/S012345678");
}

TEST(RuleTest, ToStringRoundTripsEveryPreset)
{
    for (const NamedRule& preset : kRulePresets)
    {
        EXPECT_TRUE(Rule::parse(preset.rule.toString()) == preset.rule) << preset.name;
    }
}

TEST(RuleTest, ToStringRoundTripsRandomMasks)
{
    SplitMix64 random(42);
    for (int i = 0; i < 50; ++i)
    {
        const Rule rule{static_cast<Rule::Mask>(random()), static_cast<Rule::Mask>(random())};
        EXPECT_TRUE(Rule::parse(rule.toString()) == rule) << rule.toString();
    }
}

TEST(RuleTest, ConwayTruthTable)
{
    const Rule conway;
    for (unsigned n = 0; n <= 8; ++n)
    {
        SCOPED_TRACE(n);
        EXPECT_EQ(conway.nextState(false, n), n == 3 ? kAlive : kDead);
        EXPECT_EQ(conway.nextState(true, n), n == 2 || n == 3 ? kAlive : kDead);
    }
}

TEST(RuleTest, KernelMasksAgreeWithNextStateForEveryRule)
{
    int mismatches = 0;
    for (unsigned birth = 0; birth <= 0x1FF; ++birth)
    {
        for (unsigned survival = 0; survival <= 0x1FF; ++survival)
        {
            const Rule rule{static_cast<Rule::Mask>(birth), static_cast<Rule::Mask>(survival)};
            const Rule::KernelMasks masks = rule.kernelMasks();
            for (unsigned n = 0; n <= 8; ++n)
            {
                // The kernel indexes by the 3×3 sum, which counts a live centre as one more
                // neighbour.
                const bool deadDiffers =
                    ((masks.dead >> n) & 1U) != unsigned{rule.nextState(false, n)};
                const bool aliveDiffers =
                    ((masks.alive >> (n + 1)) & 1U) != unsigned{rule.nextState(true, n)};
                mismatches += (deadDiffers ? 1 : 0) + (aliveDiffers ? 1 : 0);
            }
        }
    }
    EXPECT_EQ(mismatches, 0);
}

TEST(RuleTest, FindPresetMatchesMasksNotText)
{
    const auto presetName = [](const Rule& rule) {
        const std::optional<std::size_t> index = findPreset(rule);
        return index ? kRulePresets.at(*index).name : std::string_view("none");
    };
    EXPECT_EQ(presetName(*Rule::parse("23/36")), "HighLife");  // written the legacy way
    EXPECT_EQ(presetName(*Rule::parse("B2/S")), "Seeds");
    EXPECT_FALSE(findPreset(Rule{0, 0}).has_value());
    for (std::size_t i = 0; const NamedRule& preset : kRulePresets)
    {
        EXPECT_EQ(findPreset(preset.rule), i++) << preset.name;
    }
}

}  // namespace
}  // namespace wxLife::core
