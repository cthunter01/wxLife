#include "wxLife/core/HashLife.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "wxLife/core/EmbeddedFile.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/Random.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"

namespace wxLife::core
{
namespace
{

constexpr std::uint64_t kBudget = std::uint64_t{1} << 30;

using CellSet = std::set<std::pair<UniverseCoord, UniverseCoord>>;

// Every live cell of the plane.
CellSet cellsOf(const HashLife& life)
{
    CellSet                           cells;
    const std::optional<UniverseRect> bounds = life.bounds();
    if (bounds)
    {
        life.forEachBlock(*bounds, 0, [&](UniversePos p) { cells.emplace(p.x, p.y); });
    }
    return cells;
}

// Every live cell of a dense world whose cell `centre` sits at the plane's (0, 0).
CellSet cellsOf(const World& world, CellPos centre)
{
    CellSet cells;
    for (Coord y = 0; y < world.extent().height; ++y)
    {
        for (Coord x = 0; x < world.extent().width; ++x)
        {
            if (world.at({.x = x, .y = y}) == kAlive)
            {
                cells.emplace(x - centre.x, y - centre.y);
            }
        }
    }
    return cells;
}

Pattern embedded(std::string_view file)
{
    return readPattern(embeddedPatternText(file).value()).value();
}

// Advances `generations` using its binary expansion.
void advance(HashLife& life, std::uint64_t generations)
{
    for (unsigned bit = 0; bit < 64; ++bit)
    {
        if (((generations >> bit) & 1U) != 0)
        {
            ASSERT_TRUE(life.step(bit).has_value()) << "step 2^" << bit;
        }
    }
}

// A random square of cells, half of them alive.
std::vector<CellPos> soup(Coord side, std::uint64_t seed)
{
    SplitMix64           random(seed);
    std::vector<CellPos> cells;
    cells.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    for (Coord y = 0; y < side; ++y)
    {
        for (Coord x = 0; x < side; ++x)
        {
            if (random() % 2 == 0)
            {
                cells.push_back({.x = x, .y = y});
            }
        }
    }
    return cells;
}

static_assert(HashLife::supports(Rule{}));
static_assert(HashLife::supports(Rule::parse("B1357/S1357").value()));
static_assert(!HashLife::supports(Rule::parse("B0/S8").value()));
static_assert(!HashLife::supports(Rule::parse("B0123478/S34678").value()));

TEST(HashLifeTest, MatchesTheDenseEngineOnRandomSoups)
{
    // Seeds and Replicator grow at the speed of light, one cell per generation, which is what the
    // padding before a step has to allow for.
    constexpr std::array kRules{"B3/S23",       "B36/S23",      "B2/S",     "B1357/S1357",
                                "B3678/S34678", "B35678/S5678", "B368/S245"};
    constexpr Coord      kSoup        = 24;
    constexpr Coord      kGenerations = 60;  // 16 single steps, then 4, 8 and 32 at once
    constexpr Coord      kSide        = kSoup + (2 * kGenerations) + 8;
    const CellPos        centre{.x = kSide / 2, .y = kSide / 2};
    for (const std::string_view text : kRules)
    {
        const Rule rule = Rule::parse(text).value();
        for (std::uint64_t seed = 1; seed <= 3; ++seed)
        {
            SCOPED_TRACE(std::string(text) + " seed " + std::to_string(seed));
            // A soup around the centre, so the plane's negative coordinates are used too.
            SplitMix64           random(seed);
            std::vector<CellPos> soup;
            for (Coord y = -kSoup / 2; y < kSoup / 2; ++y)
            {
                for (Coord x = -kSoup / 2; x < kSoup / 2; ++x)
                {
                    if (random() % 5 < 2)
                    {
                        soup.push_back({.x = x, .y = y});
                    }
                }
            }
            World dense({.width = kSide, .height = kSide}, rule, Topology::BOUNDED);
            dense.setCells(soup, kAlive, centre);
            HashLife life(rule, kBudget);
            life.setCells(soup, kAlive);
            ASSERT_EQ(cellsOf(life), cellsOf(dense, centre));

            const auto compareAfter = [&](unsigned exponent) {
                for (std::uint64_t g = 0; g < (std::uint64_t{1} << exponent); ++g)
                {
                    dense.step();
                }
                ASSERT_TRUE(life.step(exponent).has_value());
                ASSERT_EQ(life.generation(), dense.generation());
                ASSERT_EQ(life.population(), static_cast<std::uint64_t>(dense.population()));
                ASSERT_EQ(cellsOf(life), cellsOf(dense, centre))
                    << "generation " << life.generation();
            };
            for (int g = 0; g < 16; ++g)
            {
                compareAfter(0);
            }
            compareAfter(2);
            compareAfter(3);
            compareAfter(5);
        }
    }
}

TEST(HashLifeTest, OneBigStepEqualsManySmallOnes)
{
    const Pattern acorn = embedded("acorn.rle");
    HashLife      big(Rule{}, kBudget);
    HashLife      small(Rule{}, kBudget);
    big.setCells(acorn.cells, kAlive);
    small.setCells(acorn.cells, kAlive);
    ASSERT_TRUE(big.step(9).has_value());
    for (int g = 0; g < 512; ++g)
    {
        ASSERT_TRUE(small.step(0).has_value());
    }
    EXPECT_EQ(big.generation(), 512U);
    EXPECT_EQ(small.generation(), 512U);
    EXPECT_EQ(cellsOf(big), cellsOf(small));
}

TEST(HashLifeTest, MethuselahsSettleAsOnTheUnboundedPlane)
{
    // With no edges, every glider they throw off survives: these are the textbook final counts.
    const auto settled = [](std::string_view file, std::uint64_t generations) {
        HashLife life(Rule{}, kBudget);
        life.setCells(embedded(file).cells, kAlive);
        advance(life, generations);
        return life.population();
    };
    EXPECT_EQ(settled("rpentomino.rle", 1103), 116U);
    EXPECT_EQ(settled("acorn.rle", 5206), 633U);
}

TEST(HashLifeTest, TheGosperGunFiresForAMillionGenerations)
{
    // Once the first glider has left, each period adds one glider of 5 cells, and on the unbounded
    // plane none is ever lost. The dense engine gives the population of each phase early on.
    const Pattern gun = embedded("gosperglidergun.rle");
    World         dense({.width = 200, .height = 200}, Rule{}, Topology::BOUNDED);
    dense.setCells(gun.cells, kAlive, {.x = 10, .y = 10});
    while (dense.generation() < 90)
    {
        dense.step();
    }
    std::array<std::uint64_t, 30> phase{};  // the population at generation 90 + i
    for (std::uint64_t& population : phase)
    {
        population = static_cast<std::uint64_t>(dense.population());
        dense.step();
    }

    HashLife life(Rule{}, kBudget);
    life.setCells(gun.cells, kAlive);
    ASSERT_TRUE(life.step(20).has_value());
    const std::uint64_t n = std::uint64_t{1} << 20;
    EXPECT_EQ(life.population(), phase.at((n - 90) % 30) + (5 * ((n - 90) / 30)));
    // The oldest glider has flown a quarter of a cell per generation towards the lower right.
    // The gun's left block stays at x = 0; its shuttles move only a little.
    const UniverseRect bounds = life.bounds().value();
    EXPECT_EQ(bounds.x0, 0);
    EXPECT_GE(bounds.y0, 0);
    EXPECT_LE(bounds.y0, 2);
    EXPECT_GT(bounds.x1, static_cast<UniverseCoord>(n / 4));
    EXPECT_LT(bounds.x1, static_cast<UniverseCoord>(n / 4) + 40);
}

TEST(HashLifeTest, ThePrimerFindsEveryPrimeOnTheUnboundedPlane)
{
    // Dean Hickerson's Primer sends a lightweight spaceship to the left for each prime p. With the
    // pattern's corner at (0, 0), the ship for p crosses x = -200 in rows 230 to 260 at generation
    // 754 + 120 p, and flies on at half a cell per generation. So the ships' positions at the end
    // say which numbers the sieve let through; in a bounded world wreckage from the edges spoils
    // this after the prime 67.
    HashLife life(Rule{}, kBudget);
    life.setCells(embedded("primer.rle").cells, kAlive);
    while (life.generation() < std::uint64_t{60} * 1024)
    {
        ASSERT_TRUE(life.step(10).has_value());
    }
    const auto                 now = static_cast<std::int64_t>(life.generation());
    std::vector<UniverseCoord> xs;
    life.forEachBlock({.x0 = -now, .y0 = 230, .x1 = -150, .y1 = 261}, 0,
                      [&](UniversePos p) { xs.push_back(p.x); });
    std::ranges::sort(xs);
    std::vector<std::int64_t> found;  // one number per ship, from its leftmost cell
    for (std::size_t i = 0; i < xs.size(); ++i)
    {
        if (i == 0 || xs[i] - xs[i - 1] > 20)
        {
            found.push_back(std::lround(
                ((2.0 * static_cast<double>(xs[i] + 200)) + static_cast<double>(now - 754)) /
                120.0));
        }
    }
    const auto isPrime = [](std::int64_t n) {
        for (std::int64_t d = 2; d * d <= n; ++d)
        {
            if (n % d == 0)
            {
                return false;
            }
        }
        return n >= 2;
    };
    std::vector<std::int64_t> primes;
    for (std::int64_t n = 2; n <= (now - 754 - 400) / 120; ++n)
    {
        if (isPrime(n))
        {
            primes.push_back(n);
        }
    }
    std::ranges::sort(found);
    ASSERT_GE(found.size(), primes.size());
    EXPECT_EQ(
        std::vector(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(primes.size())),
        primes);
    EXPECT_GT(primes.size(), 90U);  // about the first 95 primes
}

TEST(HashLifeTest, AGliderTravelsFarInOneStep)
{
    const std::vector<CellPos> glider{
        {.x = 1, .y = 0}, {.x = 2, .y = 1}, {.x = 0, .y = 2}, {.x = 1, .y = 2}, {.x = 2, .y = 2}};
    HashLife life(Rule{}, kBudget);
    life.setCells(glider, kAlive, {.x = -1000, .y = 5000});
    const CellSet before = cellsOf(life);
    // Every 4 generations one cell down and to the right: 2^40 generations is 2^38 cells.
    ASSERT_TRUE(life.step(40).has_value());
    EXPECT_EQ(life.generation(), std::uint64_t{1} << 40);
    EXPECT_EQ(life.population(), 5U);
    CellSet                 expected;
    constexpr UniverseCoord kShift = UniverseCoord{1} << 38;
    for (const auto& [x, y] : before)
    {
        expected.emplace(x + kShift, y + kShift);
    }
    EXPECT_EQ(cellsOf(life), expected);
}

TEST(HashLifeTest, CellsCanBeSetAndReadAnywhere)
{
    HashLife                       life(Rule{}, kBudget);
    constexpr UniverseCoord        kFar = UniverseCoord{1} << 60;
    const std::vector<UniversePos> cells{
        {.x = 0, .y = 0}, {.x = -1, .y = -1}, {.x = kFar, .y = -kFar}, {.x = -kFar, .y = 3}};
    EXPECT_EQ(life.setCells(cells, kAlive), 4);
    EXPECT_EQ(life.setCells(cells, kAlive), 0);  // already alive
    EXPECT_EQ(life.population(), 4U);
    for (const UniversePos p : cells)
    {
        EXPECT_EQ(life.at(p), kAlive) << p.x << ", " << p.y;
    }
    EXPECT_EQ(life.at({.x = 1, .y = 0}), kDead);
    EXPECT_EQ(life.at({.x = kFar + 1, .y = -kFar}), kDead);
    EXPECT_EQ(life.bounds(), (UniverseRect{.x0 = -kFar, .y0 = -kFar, .x1 = kFar + 1, .y1 = 4}));

    // Outside the radius, and offsets so far that the sum would overflow, set nothing.
    const std::vector<UniversePos> outside{{.x = HashLife::kRadius, .y = 0},
                                           {.x = 0, .y = -HashLife::kRadius - 1}};
    EXPECT_EQ(life.setCells(outside, kAlive), 0);
    EXPECT_EQ(
        life.setCells(cells, kAlive, {.x = std::numeric_limits<UniverseCoord>::max(), .y = 0}), 0);
    EXPECT_EQ(life.population(), 4U);

    // Erasing, also relative to an offset; cells of a pattern are CellPos.
    const std::vector<CellPos> corner{{.x = 1, .y = 1}, {.x = 5, .y = 5}};
    EXPECT_EQ(life.setCells(corner, kDead, {.x = -2, .y = -2}), 1);  // (-1, -1) only
    EXPECT_EQ(life.at({.x = -1, .y = -1}), kDead);
    EXPECT_EQ(life.population(), 3U);

    life.clear();
    EXPECT_EQ(life.population(), 0U);
    EXPECT_EQ(life.generation(), 0U);
    EXPECT_FALSE(life.bounds().has_value());
    EXPECT_EQ(life.at({.x = kFar, .y = -kFar}), kDead);
}

TEST(HashLifeTest, AnEmptyPlaneOnlyAdvancesTime)
{
    HashLife life(Rule{}, kBudget);
    ASSERT_TRUE(life.step(HashLife::kMaxStepExponent).has_value());
    EXPECT_EQ(life.generation(), std::uint64_t{1} << HashLife::kMaxStepExponent);
    EXPECT_EQ(life.population(), 0U);
}

TEST(HashLifeTest, ForEachBlockCoversTheLiveCellsAtEveryScale)
{
    HashLife                 life(Rule{}, kBudget);
    SplitMix64               random(7);
    std::vector<UniversePos> cells;
    cells.reserve(400);
    for (int i = 0; i < 400; ++i)
    {
        cells.push_back({.x = static_cast<UniverseCoord>(random() % 300) - 150,
                         .y = static_cast<UniverseCoord>(random() % 200) - 120});
    }
    life.setCells(cells, kAlive);
    const UniverseRect area{.x0 = -100, .y0 = -90, .x1 = 70, .y1 = 40};
    for (const unsigned level : {0U, 1U, 3U, 5U, 8U, 12U, 20U})
    {
        // A block per live cell's block, among those that meet the area.
        const UniverseCoord size = UniverseCoord{1} << level;
        CellSet             expected;
        for (const UniversePos p : cells)
        {
            const UniverseCoord bx = floorDiv(p.x, size) * size;
            const UniverseCoord by = floorDiv(p.y, size) * size;
            if (bx < area.x1 && by < area.y1 && bx + size > area.x0 && by + size > area.y0)
            {
                expected.emplace(bx, by);
            }
        }
        CellSet visited;
        life.forEachBlock(area, level, [&](UniversePos corner) {
            EXPECT_TRUE(visited.emplace(corner.x, corner.y).second) << "visited twice";
        });
        EXPECT_EQ(visited, expected) << "level " << level;
    }
}

TEST(HashLifeTest, APatternCanReachTheEdgeOfTheUniverse)
{
    // A glider heading for the lower right corner of the part of the plane a step can use.
    const std::vector<CellPos> glider{
        {.x = 1, .y = 0}, {.x = 2, .y = 1}, {.x = 0, .y = 2}, {.x = 1, .y = 2}, {.x = 2, .y = 2}};
    constexpr UniverseCoord kEdge = UniverseCoord{1} << (HashLife::kMaxLevel - 3);
    HashLife                life(Rule{}, kBudget);
    life.setCells(glider, kAlive, {.x = kEdge - 12, .y = kEdge - 12});
    std::expected<void, HashLifeError> result;
    int                                steps = 0;
    while ((result = life.step(0)) && steps < 100)
    {
        ++steps;
    }
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HashLifeError::UNIVERSE_EDGE);
    EXPECT_GT(steps, 0);
    // The failed step changed nothing.
    EXPECT_EQ(life.generation(), static_cast<std::uint64_t>(steps));
    EXPECT_EQ(life.population(), 5U);
}

TEST(HashLifeTest, RunningOutOfMemoryChangesNothing)
{
    // The smallest budget still holds one chunk of nodes, far too few for a soup's long future.
    HashLife life(Rule{}, 0);
    life.setCells(soup(64, 3), kAlive);
    const CellSet                            before = cellsOf(life);
    const std::expected<void, HashLifeError> result = life.step(12);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HashLifeError::OUT_OF_MEMORY);
    EXPECT_EQ(life.generation(), 0U);
    EXPECT_EQ(cellsOf(life), before);
    EXPECT_LE(life.nodeCount(), life.maxNodes());
    // A small step still fits.
    EXPECT_TRUE(life.step(0).has_value());
}

TEST(HashLifeTest, ACancelledStepChangesNothing)
{
    HashLife life(Rule{}, kBudget);
    life.setCells(soup(64, 5), kAlive);
    const CellSet                            before = cellsOf(life);
    std::atomic<bool>                        cancel{true};
    const std::expected<void, HashLifeError> result = life.step(12, {.cancel = &cancel});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HashLifeError::CANCELLED);
    EXPECT_EQ(life.generation(), 0U);
    EXPECT_EQ(cellsOf(life), before);
    cancel = false;
    EXPECT_TRUE(life.step(4, {.cancel = &cancel}).has_value());
    EXPECT_EQ(life.generation(), 16U);
}

TEST(HashLifeTest, AStepThatMustNotCollectLeavesThatToTheOwner)
{
    HashLife life(Rule{}, 0);  // the smallest budget
    life.setCells(soup(64, 3), kAlive);
    const CellSet                            before = cellsOf(life);
    const std::expected<void, HashLifeError> result = life.step(12, {.collectGarbage = false});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), HashLifeError::OUT_OF_MEMORY);
    EXPECT_EQ(cellsOf(life), before);
    // The failed step's nodes are still there until the owner collects them.
    const std::size_t full = life.nodeCount();
    life.collectGarbage();
    EXPECT_LT(life.nodeCount(), full);
    EXPECT_TRUE(life.step(0, {.collectGarbage = false}).has_value());
}

TEST(HashLifeTest, OtherThreadsCanDrawWhileAStepRuns)
{
    // As the UI will: one thread steps without collecting, another reads the plane as it was
    // before each step or after it. The reader must only ever see whole generations. Under
    // ThreadSanitizer (the tsan preset) this also checks that no memory is shared unsafely.
    HashLife life(Rule{}, kBudget);
    life.setCells(soup(48, 11), kAlive);
    constexpr UniverseRect     kEverything{.x0 = -4096, .y0 = -4096, .x1 = 4096, .y1 = 4096};
    std::vector<std::uint64_t> populations{life.population()};
    std::atomic<bool>          done{false};
    std::vector<std::uint64_t> seen;
    std::thread                reader([&] {
        while (!done)
        {
            std::uint64_t cells = 0;
            life.forEachBlock(kEverything, 0, [&](UniversePos) { ++cells; });
            seen.push_back(cells);
            static_cast<void>(life.bounds());
        }
    });
    for (int g = 0; g < 200; ++g)
    {
        ASSERT_TRUE(life.step(0, {.collectGarbage = false}).has_value());
        populations.push_back(life.population());
    }
    done = true;
    reader.join();
    ASSERT_FALSE(seen.empty());
    for (const std::uint64_t cells : seen)
    {
        EXPECT_TRUE(std::ranges::contains(populations, cells)) << cells << " cells";
    }
}

TEST(HashLifeTest, GarbageCollectionKeepsThePattern)
{
    HashLife collected(Rule{}, kBudget);
    HashLife kept(Rule{}, kBudget);
    for (HashLife* life : {&collected, &kept})
    {
        life->setCells(embedded("rabbits.rle").cells, kAlive);
        ASSERT_TRUE(life->step(8).has_value());
    }
    const std::size_t before = collected.nodeCount();
    collected.collectGarbage();
    EXPECT_LT(collected.nodeCount(), before);
    EXPECT_EQ(cellsOf(collected), cellsOf(kept));
    for (HashLife* life : {&collected, &kept})
    {
        ASSERT_TRUE(life->step(8).has_value());
    }
    EXPECT_EQ(cellsOf(collected), cellsOf(kept));
}

TEST(HashLifeTest, AChangedRuleAppliesFromTheNextStep)
{
    // The HighLife replicator copies itself under B36/S23 and dies out under Conway's rule.
    const std::vector<CellPos> replicator{{.x = 2, .y = 0}, {.x = 3, .y = 0}, {.x = 4, .y = 0},
                                          {.x = 1, .y = 1}, {.x = 4, .y = 1}, {.x = 0, .y = 2},
                                          {.x = 4, .y = 2}, {.x = 0, .y = 3}, {.x = 3, .y = 3},
                                          {.x = 0, .y = 4}, {.x = 1, .y = 4}, {.x = 2, .y = 4}};
    const Rule                 highLife = Rule::parse("B36/S23").value();
    World                      dense({.width = 200, .height = 200}, Rule{}, Topology::BOUNDED);
    HashLife                   life(Rule{}, kBudget);
    const CellPos              centre{.x = 100, .y = 100};
    dense.setCells(replicator, kAlive, centre);
    life.setCells(replicator, kAlive);
    dense.step();
    ASSERT_TRUE(life.step(0).has_value());
    dense.setRule(highLife);
    life.setRule(highLife);
    EXPECT_EQ(life.rule(), highLife);
    for (int g = 0; g < 40; ++g)
    {
        dense.step();
        ASSERT_TRUE(life.step(0).has_value());
    }
    EXPECT_EQ(cellsOf(life), cellsOf(dense, centre));
}

}  // namespace
}  // namespace wxLife::core
