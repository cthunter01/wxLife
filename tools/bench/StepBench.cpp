// Headless stepping benchmark: times World::step() on a random soup.
//
//   wxLife_bench [--size WxH] [--density D] [--threads N] [--bands N] [--generations G]
//                [--engine banded|reference] [--rule B3/S23] [--bounded]

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <print>
#include <ratio>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "wxLife/core/BandedStepper.h"
#include "wxLife/core/Format.h"
#include "wxLife/core/ParallelBands.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Stepper.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/World.h"
#include "wxLife/core/WorldLimits.h"

using namespace wxLife::core;

namespace
{

struct Options
{
    Extent      size{.width = 1000, .height = 1000};
    double      density     = 0.25;
    unsigned    threads     = 0;  // at most this many threads; 0 = hardware concurrency
    unsigned    bands       = 0;  // exactly this many bands (Banded only); 0 = automatic
    int         generations = 100;
    StepperKind engine      = StepperKind::Banded;
    Rule        rule;
    Topology    topology = Topology::Torus;
};

// {0}: the engine names, {1}: kMinCellsPerBand.
constexpr std::string_view kUsage =
    "usage: wxLife_bench [--size WxH] [--density 0..1] [--threads N] [--bands N]\n"
    "                    [--generations G] [--engine {0}] [--rule B3/S23] [--bounded]\n"
    "  --threads N  at most N threads (0 = all); one band per {1} cells,\n"
    "               or 1 band below 4 of them\n"
    "  --bands N    exactly N bands instead, at most one per row (banded engine only)\n"
    "defaults: --size 1000x1000 --density 0.25 --threads 0 --generations 100\n"
    "          --engine banded --rule B3/S23, torus edges\n";

// The engine names come from kStepperKinds, so a new engine needs no change here.
void printUsage(std::FILE* stream)
{
    std::string engines;
    for (const StepperKind kind : kStepperKinds)
    {
        if (!engines.empty())
        {
            engines += '|';
        }
        for (const char c : toString(kind))
        {
            engines += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    std::print(stream, kUsage, engines, formatCount(static_cast<std::uint64_t>(kMinCellsPerBand)));
}

// Parses all of `text` as a number, or nothing. Floating point goes through std::stod(), because
// libc++ has no floating-point from_chars before LLVM 20, and Apple's then needs macOS 26. The
// bench never calls setlocale(), so stod() reads '.' as the decimal point.
template <typename Number>
std::optional<Number> parseNumber(std::string_view text)
{
    if constexpr (std::same_as<Number, double>)
    {
        try
        {
            std::size_t  used  = 0;
            const double value = std::stod(std::string(text), &used);
            return used == text.size() ? std::optional<double>(value) : std::nullopt;
        }
        catch (const std::logic_error&)  // std::invalid_argument, std::out_of_range
        {
            return std::nullopt;
        }
    }
    else
    {
        const char* const first = std::to_address(text.begin());
        const char* const last  = std::to_address(text.end());
        Number            value{};
        const auto [end, error] = std::from_chars(first, last, value);
        if (error != std::errc{} || end != last)
        {
            return std::nullopt;
        }
        return value;
    }
}

std::optional<Extent> parseSize(std::string_view text)
{
    const std::size_t x = text.find('x');
    if (x == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto width  = parseNumber<Coord>(text.substr(0, x));
    const auto height = parseNumber<Coord>(text.substr(x + 1));
    if (!width || !height)
    {
        return std::nullopt;
    }
    return Extent{.width = *width, .height = *height};
}

enum class Parsed : std::uint8_t
{
    Run,
    Help,
    Error
};

// Stores the value of option `name` in `options`. Returns false after printing the problem when the
// option is unknown or the value is bad.
bool applyOption(std::string_view name, std::string_view value, Options& options)
{
    bool ok = true;
    if (name == "--size")
    {
        const auto size = parseSize(value);
        ok              = size.has_value();
        options.size    = size.value_or(Extent{});
    }
    else if (name == "--density")
    {
        const auto density = parseNumber<double>(value);
        ok                 = density && *density >= 0.0 && *density <= 1.0;
        options.density    = density.value_or(0.0);
    }
    else if (name == "--threads")
    {
        const auto threads = parseNumber<unsigned>(value);
        ok                 = threads.has_value();
        options.threads    = threads.value_or(0);
    }
    else if (name == "--bands")
    {
        const auto bands = parseNumber<unsigned>(value);
        ok               = bands && *bands > 0;
        options.bands    = bands.value_or(0);
    }
    else if (name == "--generations")
    {
        const auto generations = parseNumber<int>(value);
        ok                     = generations && *generations > 0;
        options.generations    = generations.value_or(0);
    }
    else if (name == "--engine")
    {
        const auto engine = parseStepperKind(value);
        ok                = engine.has_value();
        options.engine    = engine.value_or(StepperKind::Banded);
    }
    else if (name == "--rule")
    {
        const auto rule = Rule::parse(value);
        if (!rule)
        {
            std::println(stderr, "--rule {}: {}", value, describe(rule.error()));
            return false;
        }
        options.rule = *rule;
    }
    else
    {
        std::println(stderr, "unknown option {}", name);
        printUsage(stderr);
        return false;
    }
    if (!ok)
    {
        std::println(stderr, "bad value for {}: {}", name, value);
        printUsage(stderr);
    }
    return ok;
}

// Reads the command line into `options`. Prints the usage for --help, and the problem for a bad
// argument.
Parsed parseArguments(std::span<char*> args, Options& options)
{
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string_view name = args[i];
        if (name == "--bounded")
        {
            options.topology = Topology::Bounded;
            continue;
        }
        if (name == "--help" || name == "-h")
        {
            printUsage(stdout);
            return Parsed::Help;
        }
        if (i + 1 == args.size())
        {
            std::println(stderr, "{} needs a value", name);
            printUsage(stderr);
            return Parsed::Error;
        }
        if (!applyOption(name, args[++i], options))
        {
            return Parsed::Error;
        }
    }
    if (options.bands > 0 && options.engine != StepperKind::Banded)
    {
        std::println(stderr, "--bands works only with --engine banded");
        return Parsed::Error;
    }
    return Parsed::Run;
}

std::unique_ptr<Stepper> makeEngine(const Options& options)
{
    if (options.bands > 0)  // one band per cell at most, so `bands` alone sets the count
    {
        return std::make_unique<BandedStepper>(options.bands, 1);
    }
    return makeStepper(options.engine, options.threads);
}

// Bands per generation, computed as the engine computes them.
unsigned bandCount(const Options& options)
{
    const CellCount cells = options.size.cellCount();
    switch (options.engine)
    {
        case StepperKind::Banded:
        {
            const unsigned bands = options.bands > 0 ? suggestedBandCount(cells, options.bands, 1)
                                                     : suggestedBandCount(cells, options.threads);
            // forEachBand() caps it too.
            return std::min(bands, static_cast<unsigned>(options.size.height));
        }
        case StepperKind::Reference:
            return 1;
    }
    std::unreachable();
}

// The benchmark; returns the exit status.
int run(std::span<char*> args)
{
    Options      options;
    const Parsed parsed = parseArguments(args.subspan(1), options);
    if (parsed != Parsed::Run)
    {
        return parsed == Parsed::Help ? 0 : 2;
    }

    const std::uint64_t budget = defaultMemoryBudget();
    if (const auto valid = validateExtent(options.size, budget); !valid)
    {
        std::println(stderr, "--size {}x{}: {}", options.size.width, options.size.height,
                     describe(valid.error(), options.size, budget));
        return 2;
    }

    const CellCount cells = options.size.cellCount();
    const unsigned  bands = bandCount(options);
    std::println("world   {}x{} {}, {}, density {}, {}", options.size.width, options.size.height,
                 toString(options.topology), options.rule.toString(), options.density,
                 formatBytes(worldBytes(options.size)));
    std::println("engine  {}, {} band{}", toString(options.engine), bands, bands == 1 ? "" : "s");

    try
    {
        World world(options.size, options.rule, options.topology);
        world.setStepper(makeEngine(options));
        world.randomize(options.density, 1);

        using Milliseconds = std::chrono::duration<double, std::milli>;
        const auto start   = std::chrono::steady_clock::now();
        for (int g = 0; g < options.generations; ++g)
        {
            world.step();
        }
        const Milliseconds total = std::chrono::steady_clock::now() - start;

        const double msPerGeneration = total.count() / options.generations;
        std::println("result  {} generations in {:.1f} ms, population {}", options.generations,
                     total.count(), formatCount(static_cast<std::uint64_t>(world.population())));
        std::println("        {:.3f} ms/gen, {:.3f} ns/cell, {:.0f} gen/s", msPerGeneration,
                     msPerGeneration * 1e6 / static_cast<double>(cells), 1000.0 / msPerGeneration);
    }
    catch (const std::bad_alloc&)
    {
        std::println(stderr, "not enough memory for {}", formatBytes(worldBytes(options.size)));
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    try
    {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
