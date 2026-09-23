# Demo patterns

The pattern files behind **File → Demo Patterns…**. The build embeds them into the program
(`cmake/EmbedPatterns.cmake`), so wxLife needs no data files at run time. `src/core/Demo.cpp` holds the
catalogue: each demo's name, credit, description and the world it runs in.

## Sources

The RLE files come from the pattern collection of [LifeWiki](https://conwaylife.com/wiki/), as mirrored at
[copy.sh/life/examples](https://copy.sh/life/examples/). Each keeps its `#N` (name), `#O` (author) and
`#C` (comment) lines. The only changes are LF line ends, no trailing blanks and a final newline.

The giant patterns are macrocell files (`.mc`), the quadtree format of [Golly](https://golly.sourceforge.io/)
and HashLife, kept gzip-compressed (`.mc.gz`) and unpacked when a demo is loaded:
- `caterpillar`, `centipede`, `gemini`, `picalculator` and `succ` are LifeWiki's pattern files, taken
  from the Internet Archive's copies of `conwaylife.com/patterns/` (LifeWiki itself now asks browsers to
  pass a check first), and compressed with `gzip -9n`, which leaves out the name and the time.
- `metapixel-galaxy.mc.gz` and `demonoid-c512-hashlife-friendly.mc.gz` come unchanged from Golly's own
  pattern collection (`Patterns/HashLife/`).

`spaceshiprace.rle` was assembled for wxLife from `lwss.rle`, `mwss.rle`, `hwss.rle`, `weekender.rle`,
`spider.rle`, `loafer.rle` and `copperhead.rle`, with the westbound ships turned to head north.

| File | Pattern | Found or built by |
|---|---|---|
| `glider.rle` | Glider | Richard K. Guy |
| `lwss.rle` | Lightweight spaceship | John Conway |
| `spaceshiprace.rle` | Spaceship race | see above |
| `canadagoose.rle` | Canada goose | Jason Summers |
| `spider.rle` | Spider | David Bell |
| `weekender.rle` | Weekender | David Eppstein |
| `loafer.rle` | Loafer | Josh Ball |
| `copperhead.rle` | Copperhead | 'zdr' |
| `gosperglidergun.rle` | Gosper glider gun | Bill Gosper |
| `gosperglidergun_synth.rle` | Eight-glider synthesis of the Gosper gun | |
| `simkinglidergun.rle` | Simkin glider gun | Michael Simkin |
| `bigun.rle` | Bi-gun | Bill Gosper |
| `b52bomber.rle` | B-52 bomber | Noam Elkies |
| `gunstar.rle` | Gunstar | David Buckingham |
| `puffer-train.rle` | Puffer train | |
| `blinkerpuffer1.rle` | Blinker puffer 1 | Robert Wainwright |
| `spacerake.rle` | Space rake | |
| `backrake1.rle` | Backrake 1 | Jason Summers |
| `spider-rake.rle` | p360 spider rake | |
| `breeder1.rle` | Breeder 1 | Bill Gosper |
| `rileysbreeder.rle` | Riley's breeder | Mitchell Riley |
| `switch-engine-breeder.rle` | Switch-engine breeder | Helmut Postl |
| `rpentomino.rle` | R-pentomino | John Conway |
| `diehard.rle` | Die hard | |
| `acorn.rle` | Acorn | Charles Corderman |
| `rabbits.rle` | Rabbits | Andrew Trevorrow |
| `lidka.rle` | Lidka | Andrzej Okrasinski and David Bell |
| `pulsar.rle` | Pulsar | John Conway |
| `pentadecathlon.rle` | Pentadecathlon | John Conway |
| `koksgalaxy.rle` | Kok's galaxy | Jan Kok |
| `tumbler.rle` | Tumbler | George Collins |
| `queenbeeshuttle.rle` | Queen bee shuttle | Bill Gosper |
| `twinbeesshuttle.rle` | Twin bees shuttle | Bill Gosper |
| `primer.rle` | Primer | Dean Hickerson |
| `twinprimecalculator.rle` | Twin prime calculator | Dean Hickerson |
| `pp8primecalculator.rle` | (p, p+8) prime calculator | Nathaniel Johnston |
| `turingmachine.rle` | Turing machine | Paul Rendell |
| `universalturingmachine.rle` | Universal Turing machine | Paul Rendell |
| `caterpillar.mc.gz` | Caterpillar | Gabriel Nivasch, Jason Summers and David Bell |
| `centipede.mc.gz` | Centipede | Chris Cain |
| `demonoid-c512-hashlife-friendly.mc.gz` | HashLife-friendly c/512 Demonoid | Dave Greene, after Chris Cain and Dave Greene |
| `gemini.mc.gz` | Gemini | Andrew J. Wade |
| `picalculator.mc.gz` | Pi calculator | Adam P. Goucher |
| `succ.mc.gz` | Spartan universal computer-constructor | Adam P. Goucher |
| `metapixel-galaxy.mc.gz` | Kok's galaxy in OTCA metapixels | Brice Due (metapixel), Jan Kok (galaxy) |

## Adding a demo

1. Put the file here: RLE, which must be ASCII (the embedding step checks), or a macrocell file, which
   may be gzip-compressed (`.mc.gz`).
2. Add its name to `demo_patterns` in `src/CMakeLists.txt`.
3. Add a `Demo` to `kDemos` in `src/core/Demo.cpp`, in its category's group. For a fixed-size world,
   choose the world by running the pattern: it must fit, and whatever it sends to the edges must not
   come back and spoil what the description promises. A macrocell needs `.kind = WorldKind::UNBOUNDED`;
   choose its step size and `memoryNeeded` by running it too, with a memory budget like a user's (4 GiB
   on a 16 GB computer). Its first steps are the slowest, while HashLife learns the pattern.

`DemoTest` (`tests/core/DemoTests.cpp`) then checks that the file is embedded and readable, that the
pattern, the view and any ants fit the world, and that no embedded file is left without a demo.

The Orthogonoid (Dave Greene, 2017) is not here: its period, 3,476,016 generations, is no power of two,
so HashLife's steps of 2^k generations can reuse little of what they learn. It took a minute or more per
step of 2^20 generations, with 16 GiB of memory, where the Demonoid, built for HashLife, takes
milliseconds.
