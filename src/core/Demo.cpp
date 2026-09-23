#include "wxLife/core/Demo.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "wxLife/core/Ant.h"
#include "wxLife/core/EmbeddedFile.h"
#include "wxLife/core/Pattern.h"
#include "wxLife/core/Speed.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

namespace
{

[[nodiscard]] constexpr Speed perSecond(int generations) noexcept
{
    return {.gensPerSecond = generations};
}

/// Max; leaving it drops to 60 gen/s.
constexpr Speed kMaxSpeed{.gensPerSecond = 60, .unlimited = true};

constexpr std::uint64_t kGiB = std::uint64_t{1} << 30;

// The ant demos start on an empty world, so the ants are all there is.
constexpr std::array kLangtonsAnt{
    Ant{.position = {.x = 80, .y = 80}, .heading = Heading::NORTH},
};
constexpr std::array kTwoAnts{
    Ant{.position = {.x = 70, .y = 80}, .heading = Heading::NORTH},
    Ant{.position = {.x = 90, .y = 80}, .heading = Heading::NORTH},
};
// Each ant is the one before it turned a quarter clockwise about the centre (60, 60).
constexpr std::array kFourAnts{
    Ant{.position = {.x = 50, .y = 50}, .heading = Heading::NORTH},
    Ant{.position = {.x = 70, .y = 50}, .heading = Heading::EAST},
    Ant{.position = {.x = 70, .y = 70}, .heading = Heading::SOUTH},
    Ant{.position = {.x = 50, .y = 70}, .heading = Heading::WEST},
};
constexpr std::array kErasingAnts{
    Ant{.position = {.x = 20, .y = 20}, .heading = Heading::NORTH},
    Ant{.position = {.x = 21, .y = 20}, .heading = Heading::SOUTH},
};

// The settings come from running each demo headlessly: the guns' and puffers' debris vanishes at
// the dead edges, the methuselahs evolve exactly as they would on an unbounded plane, and the prime
// calculators' outputs were checked against the primes for as long as the edges allow.
constexpr std::array kDemos{
    // Spaceships. The worlds wrap, so a ship flies on forever.
    Demo{
        .name     = "Glider",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "Richard K. Guy",
        .about = "The smallest spaceship, and the first one found. Every 4 generations it is back "
                 "in its original shape, one cell further down and to the right. The world wraps "
                 "around, so it flies out of one edge and back in at the opposite one.",
        .file  = "glider.rle",
        .world = {.width = 40, .height = 40},
        .topology = Topology::TORUS,
        .speed    = perSecond(10),
    },
    Demo{
        .name     = "Lightweight spaceship",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "John Conway, 1970",
        .about    = "The smallest spaceship that travels straight: every 4 generations it moves 2 "
                    "cells to the left. That is c/2, half a cell per generation, the fastest any "
                    "spaceship can go along a row.",
        .file     = "lwss.rle",
        .world    = {.width = 60, .height = 24},
        .topology = Topology::TORUS,
        .speed    = perSecond(10),
    },
    Demo{
        .name     = "Spaceship race",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "John Conway, David Eppstein, David Bell, Josh Ball and 'zdr'",
        .about    = "Seven spaceships leave the same line together, heading up. From the left: the "
                    "lightweight, middleweight and heavyweight spaceships (c/2, 2 cells every 4 "
                    "generations), the weekender (2c/7), the spider (c/5), the loafer (c/7) and "
                    "the copperhead (c/10). The world wraps, so the fast ones soon lap the slow "
                    "ones.",
        .file     = "spaceshiprace.rle",
        .world    = {.width = 138, .height = 160},
        .topology = Topology::TORUS,
        .origin   = CellPos{.x = 8, .y = 140},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "Canada goose",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "Jason Summers, 1999",
        .about    = "A diagonal spaceship with the glider's speed, one cell up and one to the left "
                    "every 4 generations, but made of 36 cells. When it was found it was the "
                    "smallest known diagonal spaceship other than the glider.",
        .file     = "canadagoose.rle",
        .world    = {.width = 60, .height = 60},
        .topology = Topology::TORUS,
        .speed    = perSecond(15),
    },
    Demo{
        .name     = "Spider",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "David Bell, 1997",
        .about    = "A spaceship that moves one cell up every 5 generations (c/5).",
        .file     = "spider.rle",
        .world    = {.width = 60, .height = 100},
        .topology = Topology::TORUS,
        .speed    = perSecond(15),
    },
    Demo{
        .name     = "Weekender",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "David Eppstein, 2000",
        .about    = "A mirror-symmetric spaceship that moves 2 cells up every 7 generations "
                    "(2c/7).",
        .file     = "weekender.rle",
        .world    = {.width = 48, .height = 100},
        .topology = Topology::TORUS,
        .speed    = perSecond(15),
    },
    Demo{
        .name     = "Loafer",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "Josh Ball, 2013",
        .about    = "A slow spaceship, one cell to the left every 7 generations (c/7). Only 20 "
                    "cells, yet not found until 2013.",
        .file     = "loafer.rle",
        .world    = {.width = 90, .height = 30},
        .topology = Topology::TORUS,
        .speed    = perSecond(15),
    },
    Demo{
        .name     = "Copperhead",
        .category = DemoCategory::SPACESHIPS,
        .credit   = "'zdr', 2016",
        .about    = "A small, slow spaceship, one cell up every 10 generations (c/10), found only "
                    "in 2016.",
        .file     = "copperhead.rle",
        .world    = {.width = 40, .height = 90},
        .topology = Topology::TORUS,
        .speed    = perSecond(20),
    },

    // Giant spaceships, from macrocell files, on unbounded planes. The step sizes and memory needs
    // come from running each one: the first steps fill HashLife's memory, the later ones are fast.
    Demo{
        .name     = "Caterpillar",
        .category = DemoCategory::GIANT_SPACESHIPS,
        .credit   = "Gabriel Nivasch, Jason Summers and David Bell, 2004",
        .about    = "A spaceship of about 12 million cells, 4,195 by 330,721, moving up at 17c/45: "
                    "every 270 generations it is back in its own shape, 102 cells further on. "
                    "Lightweight spaceships made near its back take almost 2.7 million "
                    "generations to reach the front, where they feed the reaction that drives it; "
                    "zoom in on the front end to watch it. The first steps take a while, and it "
                    "needs about 2 GB of memory to run smoothly.",
        .file     = "caterpillar.mc.gz",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 10,
        .memoryNeeded = 2 * kGiB,
    },
    Demo{
        .name         = "Centipede",
        .category     = DemoCategory::GIANT_SPACESHIPS,
        .credit       = "Chris Cain, 2014",
        .about        = "A 31c/240 spaceship of about 620,000 cells, 11,652 by 126,714, moving up: "
                        "every 240 generations it is 31 cells further on. It reuses most of the "
                        "circuitry of the shield bug, built the same day, behind a more compact front "
                        "end. It needs about 1 GB of memory to run smoothly.",
        .file         = "centipede.mc.gz",
        .kind         = WorldKind::UNBOUNDED,
        .speed        = kMaxSpeed,
        .stepExponent = 10,
        .memoryNeeded = kGiB,
    },
    Demo{
        .name     = "Demonoid",
        .category = DemoCategory::GIANT_SPACESHIPS,
        .credit   = "Chris Cain and Dave Greene, 2015; this version by Dave Greene, 2019",
        .about    = "A self-constructing spaceship. A stream of gliders drives its construction "
                    "arm, which builds a new copy of the machinery ahead of it, while the old "
                    "copy behind is torn down. This version moves 4,096 cells up and to the right "
                    "every 2,097,152 (2^21) generations, which suits HashLife: once the first "
                    "cycle has been worked out, which takes a while, it flies.",
        .file     = "demonoid-c512-hashlife-friendly.mc.gz",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 16,
        .memoryNeeded = kGiB,
    },
    Demo{
        .name     = "Gemini",
        .category = DemoCategory::GIANT_SPACESHIPS,
        .credit   = "Andrew J. Wade, 2010",
        .about    = "The first oblique spaceship ever built. Its two identical halves, each with "
                    "three construction arms, pass a tape of gliders between them that tells each "
                    "to delete its parent and build its daughter. Every 33,699,586 generations it "
                    "has moved 5,120 cells one way and 1,024 the other. It spans more than 4 "
                    "million cells each way but has only 846,278 live cells. It is slow to run: a "
                    "step of 65,536 generations takes a second or more, and it needs about 4 GB of "
                    "memory.",
        .file     = "gemini.mc.gz",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 16,
        .memoryNeeded = 4 * kGiB,
    },

    // Guns. The gliders die cleanly at the dead edges, so each gun fires forever.
    Demo{
        .name     = "Gosper glider gun",
        .category = DemoCategory::GUNS,
        .credit   = "Bill Gosper, 1970",
        .about    = "The first gun, and the first pattern known to grow forever: a new glider "
                    "leaves every 30 generations. Two queen bee shuttles do the work, bouncing "
                    "between blocks. The gliders vanish at the lower right edge of this world.",
        .file     = "gosperglidergun.rle",
        .world    = {.width = 320, .height = 240},
        .origin   = CellPos{.x = 4, .y = 4},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "Gosper gun from eight gliders",
        .category = DemoCategory::GUNS,
        .about    = "Eight gliders collide and build a Gosper glider gun, which then starts firing "
                    "gliders of its own: a gun can be made from the very thing it makes.",
        .file     = "gosperglidergun_synth.rle",
        .world    = {.width = 320, .height = 240},
        .origin   = CellPos{.x = 4, .y = 4},
        .speed    = perSecond(15),
    },
    Demo{
        .name     = "Simkin glider gun",
        .category = DemoCategory::GUNS,
        .credit   = "Michael Simkin, 2015",
        .about    = "A gun with 36 cells, as many as Gosper's, that fires a glider to the upper "
                    "left every 120 generations.",
        .file     = "simkinglidergun.rle",
        .world    = {.width = 320, .height = 240},
        .origin   = CellPos{.x = 283, .y = 215},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "Bi-gun",
        .category = DemoCategory::GUNS,
        .credit   = "Bill Gosper",
        .about    = "A double-barrelled gun: every 46 generations it fires two gliders in opposite "
                    "directions.",
        .file     = "bigun.rle",
        .world    = {.width = 400, .height = 400},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "B-52 bomber",
        .category = DemoCategory::GUNS,
        .credit   = "Noam Elkies",
        .about    = "A double-barrelled gun of period 104: it fires a glider every 52 generations, "
                    "alternately to the upper left and the lower right.",
        .file     = "b52bomber.rle",
        .world    = {.width = 400, .height = 400},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "Gunstar",
        .category = DemoCategory::GUNS,
        .credit   = "David Buckingham, 1990",
        .about    = "Four barrels: gliders leave in all four diagonal directions, a new one from "
                    "each barrel every 144 generations.",
        .file     = "gunstar.rle",
        .world    = {.width = 600, .height = 600},
        .speed    = perSecond(30),
    },

    // Puffers and rakes: moving patterns that leave something behind. Each crosses its world
    // once and then crashes into the far edge.
    Demo{
        .name     = "Puffer train",
        .category = DemoCategory::PUFFERS_AND_RAKES,
        .about    = "A B-heptomino, which on its own would soon destroy itself, escorted to the "
                    "right by two lightweight spaceships. It leaves a cloud of smoke and debris "
                    "behind that burns for a long time.",
        .file     = "puffer-train.rle",
        .world    = {.width = 1200, .height = 360},
        .origin   = CellPos{.x = 8, .y = 171},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "Blinker puffer",
        .category = DemoCategory::PUFFERS_AND_RAKES,
        .credit   = "Robert Wainwright",
        .about    = "The first blinker puffer found: it moves left at c/2 and leaves a neat row of "
                    "blinkers behind.",
        .file     = "blinkerpuffer1.rle",
        .world    = {.width = 600, .height = 60},
        .origin   = CellPos{.x = 583, .y = 21},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "Space rake",
        .category = DemoCategory::PUFFERS_AND_RAKES,
        .about    = "A rake: a spaceship that fires gliders as it travels. It moves right at c/2 "
                    "and sends a glider up and to the right every 20 generations.",
        .file     = "spacerake.rle",
        .world    = {.width = 800, .height = 400},
        .origin   = CellPos{.x = 8, .y = 373},
        .speed    = perSecond(30),
    },
    Demo{
        .name     = "Backrake",
        .category = DemoCategory::PUFFERS_AND_RAKES,
        .credit   = "Jason Summers",
        .about  = "A rake that fires backwards: it moves up at c/2 and throws a glider down and to "
                  "the left every 8 generations, laying a long diagonal line of them.",
        .file   = "backrake1.rle",
        .world  = {.width = 600, .height = 900},
        .origin = CellPos{.x = 565, .y = 860},
        .speed  = perSecond(30),
    },
    Demo{
        .name     = "Spider rake",
        .category = DemoCategory::PUFFERS_AND_RAKES,
        .about  = "Nine gliders circle in a figure-eight loop held by c/5 spiders; every lap frees "
                  "one of them. The rake creeps up one cell every 5 generations, and the freed "
                  "gliders fall behind it.",
        .file   = "spider-rake.rle",
        .world  = {.width = 800, .height = 1000},
        .origin = CellPos{.x = 150, .y = 865},
        .speed  = perSecond(60),
    },

    // Breeders: their population grows with the square of time.
    Demo{
        .name     = "Breeder 1",
        .category = DemoCategory::BREEDERS,
        .credit   = "Bill Gosper",
        .about  = "The first pattern found whose population grows quadratically. A row of puffers "
                  "moves right at c/2 and leaves glider guns behind, and the guns' gliders fill a "
                  "growing triangle above its track.",
        .file   = "breeder1.rle",
        .world  = {.width = 3000, .height = 1500},
        .origin = CellPos{.x = 8, .y = 1154},
        .speed  = kMaxSpeed,
    },
    Demo{
        .name     = "Riley's breeder",
        .category = DemoCategory::BREEDERS,
        .credit   = "Mitchell Riley, 2006",
        .about    = "Only 38 cells, yet its population grows quadratically: as it moves right it "
                    "leaves puffers behind, and each of them lays debris of its own, filling a "
                    "slanted band.",
        .file     = "rileysbreeder.rle",
        .world    = {.width = 3000, .height = 1200},
        .origin   = CellPos{.x = 8, .y = 40},
        .speed    = kMaxSpeed,
    },
    Demo{
        .name     = "Switch-engine breeder",
        .category = DemoCategory::BREEDERS,
        .credit   = "Helmut Postl, 1997",
        .about    = "A puffer train that moves right and makes a new block-laying switch engine "
                    "every 80 generations. The switch engines fill half a quadrant with blocks.",
        .file     = "switch-engine-breeder.rle",
        .world    = {.width = 3000, .height = 900},
        .origin   = CellPos{.x = 8, .y = 696},
        .speed    = kMaxSpeed,
    },

    // Methuselahs: small starts that take a long time to settle. Every world is large enough that
    // the edges never touch the evolution, apart from the gliders that fly out.
    Demo{
        .name     = "R-pentomino",
        .category = DemoCategory::METHUSELAHS,
        .credit   = "John Conway",
        .about = "Five cells that keep changing for 1,103 generations, throwing off six gliders, "
                 "before they settle into still lifes and blinkers.",
        .file  = "rpentomino.rle",
        .world = {.width = 400, .height = 300},
        .speed = perSecond(60),
    },
    Demo{
        .name     = "Die hard",
        .category = DemoCategory::METHUSELAHS,
        .about    = "Seven cells that vanish completely, but only after 130 generations.",
        .file     = "diehard.rle",
        .world    = {.width = 60, .height = 40},
        .speed    = perSecond(10),
    },
    Demo{
        .name     = "Acorn",
        .category = DemoCategory::METHUSELAHS,
        .credit   = "Charles Corderman",
        .about    = "Seven cells that grow for 5,206 generations and end up as 633 cells, 13 of "
                    "them gliders. Here the gliders die at the edges of the world.",
        .file     = "acorn.rle",
        .world    = {.width = 900, .height = 700},
        .speed    = perSecond(120),
    },
    Demo{
        .name     = "Rabbits",
        .category = DemoCategory::METHUSELAHS,
        .credit   = "Andrew Trevorrow",
        .about    = "Nine cells that take 17,331 generations to settle.",
        .file     = "rabbits.rle",
        .world    = {.width = 1200, .height = 900},
        .speed    = kMaxSpeed,
    },
    Demo{
        .name     = "Lidka",
        .category = DemoCategory::METHUSELAHS,
        .credit   = "Andrzej Okrasinski and David Bell",
        .about    = "Thirteen cells that take 29,055 generations to settle.",
        .file     = "lidka.rle",
        .world    = {.width = 1200, .height = 900},
        .speed    = kMaxSpeed,
    },

    // Oscillators: patterns that return to their start. Slow speeds, so each phase can be seen.
    Demo{
        .name     = "Pulsar",
        .category = DemoCategory::OSCILLATORS,
        .credit   = "John Conway, 1970",
        .about    = "Repeats every 3 generations. By far the most common oscillator with a period "
                    "above 2.",
        .file     = "pulsar.rle",
        .world    = {.width = 25, .height = 25},
        .speed    = perSecond(4),
    },
    Demo{
        .name     = "Pentadecathlon",
        .category = DemoCategory::OSCILLATORS,
        .credit   = "John Conway, 1970",
        .about    = "Repeats every 15 generations. Ten cells in a row turn into it.",
        .file     = "pentadecathlon.rle",
        .world    = {.width = 22, .height = 15},
        .speed    = perSecond(6),
    },
    Demo{
        .name     = "Kok's galaxy",
        .category = DemoCategory::OSCILLATORS,
        .credit   = "Jan Kok, 1971",
        .about    = "Four arms that swirl around the centre, repeating every 8 generations.",
        .file     = "koksgalaxy.rle",
        .world    = {.width = 21, .height = 21},
        .speed    = perSecond(5),
    },
    Demo{
        .name     = "Tumbler",
        .category = DemoCategory::OSCILLATORS,
        .credit   = "George Collins",
        .about    = "Flips over and back every 14 generations: the smallest known oscillator with "
                    "that period.",
        .file     = "tumbler.rle",
        .world    = {.width = 21, .height = 17},
        .speed    = perSecond(6),
    },
    Demo{
        .name     = "Queen bee shuttle",
        .category = DemoCategory::OSCILLATORS,
        .credit   = "Bill Gosper",
        .about    = "A queen bee runs back and forth between two blocks, which stop it from "
                    "exploding; period 30. The Gosper glider gun is two of these.",
        .file     = "queenbeeshuttle.rle",
        .world    = {.width = 36, .height = 21},
        .speed    = perSecond(10),
    },
    Demo{
        .name     = "Twin bees shuttle",
        .category = DemoCategory::OSCILLATORS,
        .credit   = "Bill Gosper, 1971",
        .about    = "Two bees shuttle back and forth between blocks, repeating every 46 "
                    "generations.",
        .file     = "twinbeesshuttle.rle",
        .world    = {.width = 43, .height = 25},
        .speed    = perSecond(10),
    },

    // Computation. The prime calculators send streams up and to the right, which an unbounded
    // plane swallows; in a fixed-size world they would crash into the edges and come back as
    // wreckage.
    Demo{
        .name     = "Primer",
        .category = DemoCategory::COMPUTATION,
        .credit   = "Dean Hickerson, 1991",
        .about    = "A prime number sieve. Every lightweight spaceship that escapes to the left "
                    "stands for a prime p and leaves at generation 120 p plus a constant, so they "
                    "come 120 generations apart for 2, 3, 5, 7, and the gaps are the numbers the "
                    "sieve has struck out. On the unbounded plane nothing comes back to spoil "
                    "it, so it goes on finding primes for as long as it runs.",
        .file     = "primer.rle",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 5,
        .view         = CellRect{.x0 = -300, .y0 = -40, .x1 = 480, .y1 = 334},
    },
    Demo{
        .name     = "Twin prime calculator",
        .category = DemoCategory::COMPUTATION,
        .credit   = "Dean Hickerson, 1994",
        .about    = "The Primer, changed so that a spaceship escapes to the left only when p and "
                    "p + 2 are both prime: for 5, 11, 17, 29, 41 and 59, each 120 p generations "
                    "after the start plus a constant, and on for as long as it runs.",
        .file     = "twinprimecalculator.rle",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 5,
        .view         = CellRect{.x0 = -300, .y0 = -40, .x1 = 480, .y1 = 334},
    },
    Demo{
        .name     = "(p, p+8) prime calculator",
        .category = DemoCategory::COMPUTATION,
        .credit   = "Nathaniel Johnston, 2009",
        .about    = "A spaceship escapes to the left for every p where p and p + 8 are both prime: "
                    "5, 11, 23, 29, 53, 59, 71, 89, 101, each 120 p generations after the start "
                    "plus a constant, and on for as long as it runs.",
        .file     = "pp8primecalculator.rle",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 5,
        .view         = CellRect{.x0 = -300, .y0 = -40, .x1 = 1062, .y1 = 658},
    },
    Demo{
        .name     = "Pi calculator",
        .category = DemoCategory::COMPUTATION,
        .credit   = "Adam P. Goucher, 2010",
        .about    = "A computer of 188 states that works out the decimal digits of pi, in binary "
                    "with a streaming spigot algorithm, and prints them as dot-matrix digits of "
                    "blocks along a diagonal stripe: 3.1… from lower left to upper right. It is "
                    "slow: each digit takes tens of billions of generations, so the steps here "
                    "are a billion generations each. The view starts on the digits; zoom out for "
                    "the whole machine, 117,573 by 155,887 cells.",
        .file     = "picalculator.mc.gz",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 30,
        // Around the first digits, relative to the corner of the bounding box.
        .view         = CellRect{.x0 = 70472, .y0 = -2679, .x1 = 74472, .y1 = 1321},
        .memoryNeeded = 2 * kGiB,
    },
    Demo{
        .name     = "Spartan universal computer-constructor",
        .category = DemoCategory::COMPUTATION,
        .credit   = "Adam P. Goucher, 2009",
        .about    = "A universal computer with a construction arm, built only from still lifes of "
                    "seven cells or fewer. It has eleven sliding-block registers, eight two-state "
                    "gates, a read-only program tape of eaters and two tapes of blocks it can "
                    "change. With a long enough program it could build a copy of itself. It "
                    "computes very slowly; zoom in on the machinery at the upper left and let it "
                    "run.",
        .file     = "succ.mc.gz",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 20,
        .memoryNeeded = kGiB,
    },
    Demo{
        .name     = "Turing machine",
        .category = DemoCategory::COMPUTATION,
        .credit   = "Paul Rendell, 2000",
        .about    = "A Turing machine made of guns, glider streams and still lifes. The grid of "
                    "nine memory cells at the lower left is its finite-state machine; the tape is "
                    "kept in the two long diagonal stacks, and one step of the machine takes "
                    "thousands of generations.",
        .file     = "turingmachine.rle",
        .world    = {.width = 1900, .height = 1840},
        .speed    = kMaxSpeed,
    },
    Demo{
        .name     = "Universal Turing machine",
        .category = DemoCategory::COMPUTATION,
        .credit   = "Paul Rendell, 2010",
        .about    = "A Turing machine that runs other Turing machines, described on its tape. The "
                    "view starts on its finite-state machine, the grid in the middle; the tape "
                    "runs along the long diagonal. The world needs about 330 MB of memory.",
        .file     = "universalturingmachine.rle",
        .world    = {.width = 12800, .height = 12800},
        .speed    = kMaxSpeed,
        .view     = CellRect{.x0 = 4950, .y0 = 5526, .x1 = 7150, .y1 = 8426},
    },

    // Life in Life.
    Demo{
        .name     = "Kok's galaxy in OTCA metapixels",
        .category = DemoCategory::LIFE_IN_LIFE,
        .credit   = "Metapixel by Brice Due, 2006; galaxy by Jan Kok, 1971",
        .about    = "Life simulating itself. Each of the 15 by 15 cells here is an OTCA metapixel, "
                    "a pattern of 2,048 by 2,048 cells that behaves like one cell of any Life-like "
                    "rule, here Conway's: every 35,328 generations it switches on or off according "
                    "to its eight neighbours. Together they run Kok's galaxy, an oscillator of "
                    "period 8. Zoom in on a metapixel to see the machinery that counts its "
                    "neighbours.",
        .file     = "metapixel-galaxy.mc.gz",
        .kind     = WorldKind::UNBOUNDED,
        .speed    = kMaxSpeed,
        .stepExponent = 11,
    },

    // Langton's ant. The ants always wrap around the edges.
    Demo{
        .name      = "Langton's ant",
        .category  = DemoCategory::LANGTONS_ANT,
        .credit    = "Chris Langton, 1986",
        .about     = "One ant on an empty world. For about 10,000 moves it scribbles a seemingly "
                     "random blob, then suddenly builds a highway that repeats every 104 moves and "
                     "heads off diagonally. The world wraps, so the highway comes back into the "
                     "blob.",
        .world     = {.width = 160, .height = 160},
        .topology  = Topology::TORUS,
        .speed     = perSecond(500),
        .automaton = Automaton::LANGTON_ANT,
        .ants      = kLangtonsAnt,
    },
    Demo{
        .name     = "Two ants",
        .category = DemoCategory::LANGTONS_ANT,
        .about    = "Two ants start 20 cells apart, both facing up. Their scribbles merge into one "
                    "blob, and highways grow out of it across the wrapping world.",
        .world    = {.width = 160, .height = 160},
        .topology = Topology::TORUS,
        .speed    = perSecond(500),
        .automaton = Automaton::LANGTON_ANT,
        .ants      = kTwoAnts,
    },
    Demo{
        .name      = "Four ants in a square",
        .category  = DemoCategory::LANGTONS_ANT,
        .about     = "Four ants at the corners of a square, each facing a quarter turn further "
                     "round. The symmetry never breaks: they build a flower that grows and shrinks "
                     "again, and in the first 150,000 generations it never leaves a 77 by 77 "
                     "square.",
        .world     = {.width = 120, .height = 120},
        .topology  = Topology::TORUS,
        .speed     = perSecond(500),
        .automaton = Automaton::LANGTON_ANT,
        .ants      = kFourAnts,
    },
    Demo{
        .name      = "Ants that erase each other",
        .category  = DemoCategory::LANGTONS_ANT,
        .about     = "Two ants side by side, facing opposite ways. Each one undoes what the other "
                     "has just drawn, so every 24 generations the world is empty again.",
        .world     = {.width = 40, .height = 40},
        .topology  = Topology::TORUS,
        .speed     = perSecond(10),
        .automaton = Automaton::LANGTON_ANT,
        .ants      = kErasingAnts,
    },
};

}  // namespace

std::span<const Demo> demos() noexcept
{
    return kDemos;
}

Pattern demoPattern(const Demo& demo)
{
    if (demo.file.empty())
    {
        return {};
    }
    const std::optional<std::string_view> text = embeddedPatternText(demo.file);
    assert(text.has_value());  // the demo tests load every demo
    if (!text)
    {
        return {};
    }
    auto pattern = readPatternData(*text);
    assert(pattern.has_value());
    return pattern ? *std::move(pattern) : Pattern{};
}

}  // namespace wxLife::core
