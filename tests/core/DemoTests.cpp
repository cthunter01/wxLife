#include "wxLife/core/Demo.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/Ant.h"
#include "wxLife/core/EmbeddedFile.h"
#include "wxLife/core/HashLife.h"
#include "wxLife/core/Macrocell.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/PatternSetup.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"
#include "wxLife/core/WorldLimits.h"

namespace wxLife::core
{
namespace
{

constexpr std::uint64_t kMiB = std::uint64_t{1} << 20;

bool inside(UniverseRect rect, Extent world)
{
    return !rect.empty() && rect.x0 >= 0 && rect.y0 >= 0 && rect.x1 <= world.width &&
           rect.y1 <= world.height;
}

bool overlap(UniverseRect a, UniverseRect b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

// The demo's pattern, read once: the giant ones take a while in a debug build.
const Pattern& patternOf(const Demo& demo)
{
    static std::map<std::string_view, Pattern> s_read;
    const auto                                 found = s_read.find(demo.name);
    return found != s_read.end() ? found->second
                                 : s_read.emplace(demo.name, demoPattern(demo)).first->second;
}

const Demo& demoNamed(std::string_view name)
{
    const std::span<const Demo> all   = demos();
    const auto                  found = std::ranges::find(all, name, &Demo::name);
    if (found == all.end())
    {
        throw std::invalid_argument("no demo called " + std::string(name));
    }
    return *found;
}

// A world set up as the demo says.
World load(const Demo& demo)
{
    const Pattern      pattern = demoPattern(demo);
    const PatternSetup setup   = demoSetup(demo, pattern);
    World              world(setup.world, setup.rule, setup.topology);
    world.setAnts(setup.ants);
    world.setAutomaton(setup.automaton);
    world.setCells(pattern.cells, kAlive, setup.origin);
    return world;
}

TEST(DemoTest, EveryDemoReadsAndFitsItsWorld)
{
    for (const Demo& demo : demos())
    {
        SCOPED_TRACE(demo.name);
        EXPECT_FALSE(demo.name.empty());
        EXPECT_FALSE(demo.about.empty());
        EXPECT_EQ(demo.speed, demo.speed.clamped());

        const bool unbounded = demo.kind == WorldKind::UNBOUNDED;
        if (unbounded)
        {
            EXPECT_EQ(demo.world, (Extent{})) << "a plane has no size";
            EXPECT_LE(demo.stepExponent, 40U) << "the largest step the UI offers is 2^40";
            EXPECT_EQ(demo.automaton, Automaton::LIFE);
        }
        else
        {
            // Every world is possible on some computer: the budget only greys out the largest.
            EXPECT_TRUE(validateExtent(demo.world, std::uint64_t{1} << 40));
            EXPECT_EQ(demo.stepExponent, 0U);
            EXPECT_EQ(demo.memoryNeeded, 0U);
        }

        Pattern pattern;
        if (!demo.file.empty())
        {
            const std::optional<std::string_view> text = embeddedPatternText(demo.file);
            ASSERT_TRUE(text.has_value()) << demo.file << " is not embedded";
            const std::expected<Pattern, std::string> read = readPatternData(*text);
            ASSERT_TRUE(read.has_value()) << read.error();
            pattern = patternOf(demo);
            if (pattern.tree)
            {
                EXPECT_TRUE(unbounded) << "a macrocell needs an unbounded world";
                EXPECT_GT(pattern.tree->population, 0U);
            }
            else
            {
                EXPECT_FALSE(pattern.cells.empty());
            }
            EXPECT_EQ(pattern.rule.value_or(Rule{}), Rule{}) << "every demo runs Conway's Life";
        }
        const PatternSetup setup = demoSetup(demo, pattern);
        const UniverseRect placed =
            pattern.tree ? pattern.tree->bounds
                         : UniverseRect{.x0 = setup.origin.x,
                                        .y0 = setup.origin.y,
                                        .x1 = setup.origin.x + std::max(pattern.extent.width, 1),
                                        .y1 = setup.origin.y + std::max(pattern.extent.height, 1)};
        if (!unbounded)
        {
            EXPECT_TRUE(inside(placed, demo.world)) << "the pattern does not fit its world";
        }
        if (setup.view)
        {
            EXPECT_TRUE(unbounded ? overlap(setup.view.value(), placed)
                                  : inside(setup.view.value(), demo.world))
                << "the view shows nothing of the pattern, or leaves the world";
        }

        const bool ants = demo.automaton == Automaton::LANGTON_ANT;
        EXPECT_EQ(ants, !demo.ants.empty()) << "ants only for the ant, and at least one for it";
        EXPECT_LE(demo.ants.size(), static_cast<std::size_t>(kMaxAnts));
        for (const Ant& ant : demo.ants)
        {
            EXPECT_TRUE(demo.world.contains(ant.position));
        }
        // What demoPattern() gives the program is what the file says.
        if (!demo.file.empty())
        {
            EXPECT_EQ(pattern.cells, readPatternData(*embeddedPatternText(demo.file))->cells);
        }
    }
}

TEST(DemoTest, DemosAreGroupedInCategoryOrder)
{
    std::vector<DemoCategory>  order;
    std::set<std::string_view> names;
    for (const Demo& demo : demos())
    {
        if (order.empty() || order.back() != demo.category)
        {
            order.push_back(demo.category);
        }
        EXPECT_TRUE(names.insert(demo.name).second) << "two demos called " << demo.name;
    }
    // Each category once, all of them, in the order of kDemoCategories.
    EXPECT_TRUE(std::ranges::equal(order, kDemoCategories));
}

TEST(DemoTest, EveryEmbeddedFileBelongsToADemo)
{
    std::set<std::string_view> files;
    for (const EmbeddedFile& file : embeddedPatternFiles())
    {
        EXPECT_TRUE(files.insert(file.name).second) << "embedded twice: " << file.name;
        EXPECT_TRUE(std::ranges::contains(demos(), file.name, &Demo::file))
            << file.name << " is embedded but no demo uses it";
    }
    EXPECT_FALSE(embeddedPatternText("no-such-file.rle").has_value());
}

TEST(DemoTest, DemosFitAComputerWithTwoGigabytes)
{
    // A quarter of 2 GiB is the memory budget there. Only the universal Turing machine needs more
    // than the smallest budget, 256 MiB.
    for (const Demo& demo : demos())
    {
        EXPECT_LE(worldBytes(demo.world), 512 * kMiB) << demo.name;
        if (demo.name != "Universal Turing machine")
        {
            EXPECT_LE(worldBytes(demo.world), 256 * kMiB) << demo.name;
        }
    }
}

TEST(DemoTest, DemoSetupPlacesThePatternAndTheView)
{
    // The Primer runs on a plane, centred on (0, 0).
    const Demo&        primer  = demoNamed("Primer");
    const Pattern      pattern = demoPattern(primer);
    const PatternSetup setup   = demoSetup(primer, pattern);
    EXPECT_EQ(setup.kind, WorldKind::UNBOUNDED);
    EXPECT_EQ(setup.origin,
              (CellPos{.x = -(pattern.extent.width / 2), .y = -(pattern.extent.height / 2)}));
    EXPECT_EQ(setup.speed, primer.speed);
    EXPECT_EQ(setup.stepExponent, primer.stepExponent);
    EXPECT_EQ(setup.automaton, Automaton::LIFE);
    EXPECT_TRUE(setup.ants.empty());
    // The view is given relative to the pattern and set up in world cells.
    ASSERT_TRUE(setup.view.has_value());
    EXPECT_EQ(setup.view.value().x0, primer.view.value().x0 + setup.origin.x);
    EXPECT_EQ(setup.view.value().y1, primer.view.value().y1 + setup.origin.y);

    // Without an origin the pattern is centred.
    const Demo&        glider  = demoNamed("Glider");
    const PatternSetup centred = demoSetup(glider, demoPattern(glider));
    EXPECT_EQ(centred.origin, (CellPos{.x = (40 - 3) / 2, .y = (40 - 3) / 2}));
    EXPECT_EQ(centred.topology, Topology::TORUS);
    EXPECT_FALSE(centred.view.has_value());

    const Demo&        ants     = demoNamed("Four ants in a square");
    const PatternSetup antSetup = demoSetup(ants, demoPattern(ants));
    EXPECT_EQ(antSetup.automaton, Automaton::LANGTON_ANT);
    EXPECT_TRUE(std::ranges::equal(antSetup.ants, ants.ants));
}

TEST(DemoTest, TheGosperGunFiresAGliderEvery30Generations)
{
    World world = load(demoNamed("Gosper glider gun"));
    EXPECT_EQ(world.population(), 36);
    for (int glider = 1; glider <= 10; ++glider)
    {
        for (int g = 0; g < 30; ++g)
        {
            world.step();
        }
        EXPECT_EQ(world.population(), 36 + (5 * glider)) << "after " << world.generation();
    }
}

TEST(DemoTest, ThePrimerSendsASpaceshipForEachPrime)
{
    // The Primer's own world is too large for a quick test, so this one is smaller. Its edges
    // stay far enough away for the first primes. A spaceship reaches the column 200 cells left of
    // the sieve at generation 754 + 120 p for each prime p.
    const Pattern primer = demoPattern(demoNamed("Primer"));
    World         world({.width = 1100, .height = 800}, Rule{}, Topology::BOUNDED);
    const CellPos origin{.x = 300, .y = 400};
    world.setCells(primer.cells, kAlive, origin);

    std::vector<std::uint64_t> arrivals;
    bool                       passing = false;
    while (world.generation() <= 2400)
    {
        bool occupied = false;
        for (Coord y = origin.y + 230; y <= origin.y + 260; ++y)
        {
            occupied = occupied || world.at({.x = origin.x - 200, .y = y}) == kAlive;
        }
        if (occupied && !passing)
        {
            arrivals.push_back(world.generation());
        }
        passing = occupied;
        world.step();
    }
    std::vector<std::uint64_t> primes;
    for (const std::uint64_t p : {2U, 3U, 5U, 7U, 11U, 13U})
    {
        primes.push_back(754 + (120 * p));
    }
    EXPECT_EQ(arrivals, primes);
}

TEST(DemoTest, TheFourAntsStayInTheirSquare)
{
    // The description promises a 77 × 77 square, which is 22..98 in this world. Every live cell was
    // flipped by an ant standing on it, so it is enough to watch where the ants flip cells.
    World world = load(demoNamed("Four ants in a square"));
    for (int g = 0; g < 20'000; ++g)
    {
        for (const Ant& ant : world.ants())
        {
            ASSERT_TRUE(ant.position.x >= 22 && ant.position.x <= 98 && ant.position.y >= 22 &&
                        ant.position.y <= 98)
                << "generation " << world.generation();
        }
        world.step();
    }
}

TEST(DemoTest, TheErasingAntsEmptyTheWorldEvery24Generations)
{
    World world = load(demoNamed("Ants that erase each other"));
    for (int period = 1; period <= 20; ++period)
    {
        for (int g = 0; g < 24; ++g)
        {
            world.step();
        }
        EXPECT_EQ(world.population(), 0) << "generation " << world.generation();
        EXPECT_EQ(world.cells().countAlive(), 0);
    }
}

TEST(DemoTest, TheGiantPatternsAreTheOnesLifeWikiDescribes)
{
    // Bounding boxes and populations as LifeWiki gives them, where the file is in the same phase.
    struct Facts
    {
        std::string_view             demo;
        UniverseCoord                width;
        UniverseCoord                height;
        std::optional<std::uint64_t> population;
    };
    constexpr UniverseCoord kMetapixelSide = 2048;
    for (const Facts& facts : {
             Facts{.demo = "Caterpillar", .width = 4195, .height = 330'721, .population = {}},
             Facts{
                 .demo = "Gemini", .width = 4'217'807, .height = 4'220'191, .population = 846'278},
             Facts{.demo       = "Pi calculator",
                   .width      = 117'573,
                   .height     = 155'887,
                   .population = 1'189'325},
             Facts{.demo       = "Kok's galaxy in OTCA metapixels",
                   .width      = 15 * kMetapixelSide,
                   .height     = 15 * kMetapixelSide,
                   .population = {}},
             Facts{.demo       = "Spartan universal computer-constructor",
                   .width      = 84'625,
                   .height     = 73'461,
                   .population = {}},
         })
    {
        SCOPED_TRACE(facts.demo);
        const Pattern& pattern = patternOf(demoNamed(facts.demo));
        ASSERT_TRUE(pattern.tree.has_value());
        const Macrocell&   tree   = pattern.tree.value();
        const UniverseRect bounds = tree.bounds;
        EXPECT_EQ(bounds.x1 - bounds.x0, facts.width);
        EXPECT_EQ(bounds.y1 - bounds.y0, facts.height);
        if (facts.population)
        {
            EXPECT_EQ(tree.population, facts.population.value());
        }
    }
    EXPECT_EQ(patternOf(demoNamed("Centipede")).tree.value().population, 620'901U);
}

TEST(DemoTest, EveryMacrocellBecomesTheSamePlane)
{
    // What the reader works out from the file, the plane holds once it is built.
    for (const Demo& demo : demos())
    {
        const Pattern& pattern = patternOf(demo);
        if (!pattern.tree)
        {
            continue;
        }
        SCOPED_TRACE(demo.name);
        const Macrocell& tree = pattern.tree.value();
        HashLife         plane(pattern.rule.value_or(Rule{}), std::uint64_t{1} << 30);
        plane.load(tree);
        EXPECT_EQ(plane.population(), tree.population);
        EXPECT_EQ(plane.bounds(), tree.bounds);
        EXPECT_EQ(plane.generation(), tree.generation);
    }
}

}  // namespace
}  // namespace wxLife::core
