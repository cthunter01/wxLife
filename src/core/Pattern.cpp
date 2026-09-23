#include "wxLife/core/Pattern.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "wxLife/core/Format.h"
#include "wxLife/core/Gzip.h"
#include "wxLife/core/Macrocell.h"
#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"
#include "wxLife/core/WorldLimits.h"

namespace wxLife::core
{

namespace
{

constexpr std::string_view kSpace = " \t\r\n\v\f";

[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    const std::size_t first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) + 1 - first);
}

[[nodiscard]] constexpr char lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) noexcept
{
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix, {}, lower, lower);
}

[[nodiscard]] bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && startsWithIgnoringCase(a, b);
}

[[nodiscard]] constexpr bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

/// The lines of a text, without their line ends (LF or CRLF), numbered from 1.
class Lines
{
public:
    explicit Lines(std::string_view text) noexcept : m_rest(text) { }

    /// The next line, or nullopt after the last one.
    [[nodiscard]] std::optional<std::string_view> next() noexcept
    {
        if (m_done)
        {
            return std::nullopt;
        }
        ++m_number;
        const std::size_t end  = m_rest.find('\n');
        std::string_view  line = m_rest.substr(0, end);
        if (end == std::string_view::npos)
        {
            m_done = true;
            m_rest = {};
        }
        else
        {
            m_rest.remove_prefix(end + 1);
        }
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        return line;
    }

    /// The number of the line next() returned last.
    [[nodiscard]] int number() const noexcept { return m_number; }

private:
    std::string_view m_rest;
    int              m_number = 0;
    bool             m_done   = false;
};

[[nodiscard]] std::unexpected<PatternError> fail(PatternErrorKind kind, int line,
                                                 std::string detail = {})
{
    return std::unexpected(PatternError{.kind = kind, .line = line, .detail = std::move(detail)});
}

// How an unexpected character is quoted in a message: printable ASCII as itself, the rest as a
// byte value, since a lone byte of a UTF-8 sequence cannot be shown.
[[nodiscard]] std::string quoteCharacter(char c)
{
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x21 && byte < 0x7F)
    {
        return std::format("'{}'", c);
    }
    return std::format("(byte 0x{:02X})", unsigned{byte});
}

[[nodiscard]] std::string sizeText(std::int64_t width, std::int64_t height)
{
    return std::format("{} × {}", formatCount(static_cast<std::uint64_t>(width)),
                       formatCount(static_cast<std::uint64_t>(height)));
}

/// Collects the live cells and their bounding box while a reader walks the rows.
class CellCollector
{
public:
    /// Adds `count` live cells from (x, y) to the right. @return false if one of them lies beyond
    /// kMaxWorldSide.
    [[nodiscard]] bool add(std::int64_t x, std::int64_t y, std::int64_t count)
    {
        if (count <= 0)
        {
            return true;
        }
        if (x + count > kMaxWorldSide || y >= kMaxWorldSide)
        {
            return false;
        }
        for (std::int64_t i = 0; i < count; ++i)
        {
            m_cells.push_back({.x = static_cast<Coord>(x + i), .y = static_cast<Coord>(y)});
        }
        m_width  = std::max(m_width, x + count);
        m_height = std::max(m_height, y + 1);
        return true;
    }

    /// Hands the cells to `pattern`, whose extent becomes the declared size grown to contain them.
    void finish(Pattern& pattern, std::int64_t declaredWidth, std::int64_t declaredHeight)
    {
        pattern.cells  = std::move(m_cells);
        pattern.extent = {.width  = static_cast<Coord>(std::max(declaredWidth, m_width)),
                          .height = static_cast<Coord>(std::max(declaredHeight, m_height))};
    }

private:
    std::vector<CellPos> m_cells;
    std::int64_t         m_width  = 0;
    std::int64_t         m_height = 0;
};

// The rule text of an RLE header or "#r" line.
[[nodiscard]] std::expected<std::optional<Rule>, PatternError> readRule(std::string_view text,
                                                                        int              line)
{
    // Golly appends the shape of a bounded grid: "B3/S23:T100,100".
    text = trim(text.substr(0, text.find(':')));
    if (text.empty())
    {
        return std::nullopt;
    }
    if (const auto rule = Rule::parse(text))
    {
        return *rule;
    }
    if (equalsIgnoringCase(text, "Life"))
    {
        return Rule{};
    }
    for (const NamedRule& preset : kRulePresets)
    {
        if (equalsIgnoringCase(text, preset.name))
        {
            return preset.rule;
        }
    }
    return fail(PatternErrorKind::UNSUPPORTED_RULE, line, std::string(text));
}

// "x = 36, y = 9, rule = B3/S23". The rule runs to the end of the line, since Golly's suffix may
// contain a comma; other keys are ignored.
[[nodiscard]] std::expected<void, PatternError> readHeader(std::string_view text, int line,
                                                           Pattern& pattern, std::int64_t& width,
                                                           std::int64_t& height)
{
    const auto bad = [&] {
        return fail(PatternErrorKind::BAD_HEADER, line, std::string(trim(text)));
    };
    std::optional<std::int64_t> x;
    std::optional<std::int64_t> y;
    std::string_view            rest = text;
    while (!(rest = trim(rest)).empty())
    {
        const std::size_t equals = rest.find('=');
        if (equals == std::string_view::npos)
        {
            return bad();
        }
        const std::string_view key = trim(rest.substr(0, equals));
        rest.remove_prefix(equals + 1);
        if (equalsIgnoringCase(key, "rule"))
        {
            const auto rule = readRule(rest, line);
            if (!rule)
            {
                return std::unexpected(rule.error());
            }
            pattern.rule = *rule;
            break;
        }
        const std::size_t      comma = rest.find(',');
        const std::string_view value = trim(rest.substr(0, comma));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        if (!equalsIgnoringCase(key, "x") && !equalsIgnoringCase(key, "y"))
        {
            continue;
        }
        std::int64_t      number = 0;
        const char* const first  = std::to_address(value.begin());
        const char* const last   = std::to_address(value.end());
        const auto [end, error]  = std::from_chars(first, last, number);
        if (first == last || error != std::errc{} || end != last || number < 0)
        {
            return bad();
        }
        (equalsIgnoringCase(key, "x") ? x : y) = number;
    }
    if (!x || !y)
    {
        return bad();
    }
    if (*x > kMaxWorldSide || *y > kMaxWorldSide)
    {
        return fail(PatternErrorKind::TOO_LARGE, line, sizeText(*x, *y));
    }
    width  = *x;
    height = *y;
    return {};
}

// A "#" line of an RLE file. Returns an error only for an unreadable "#r" rule.
[[nodiscard]] std::expected<void, PatternError> readRleComment(std::string_view line, int number,
                                                               Pattern& pattern)
{
    const char             marker = line.size() > 1 ? line[1] : ' ';
    const std::string_view text   = trim(line.substr(std::min<std::size_t>(line.size(), 2)));
    switch (marker)
    {
        case 'N':
            if (pattern.name.empty())
            {
                pattern.name = text;
                return {};
            }
            break;
        case 'O':
            if (pattern.author.empty())
            {
                pattern.author = text;
                return {};
            }
            break;
        case 'r':
        {
            const auto rule = readRule(text, number);
            if (!rule)
            {
                return std::unexpected(rule.error());
            }
            pattern.rule = *rule;
            return {};
        }
        case 'P':
        case 'R':
            return {};  // a position, which only matters to the program that wrote it
        default:
            if (line.starts_with("#CXRLE"))
            {
                return {};  // Golly's position and generation
            }
            break;
    }
    pattern.comments.emplace_back(text);
    return {};
}

/// The declared size of an RLE pattern.
struct RleSize
{
    std::int64_t width  = 0;
    std::int64_t height = 0;
};

// The comment lines and the header of an RLE file, up to and including the header line.
[[nodiscard]] std::expected<RleSize, PatternError> readRleHead(Lines& lines, Pattern& pattern)
{
    while (const std::optional<std::string_view> line = lines.next())
    {
        const std::string_view text = trim(*line);
        if (text.empty())
        {
            continue;
        }
        if (text.starts_with('#'))
        {
            if (const auto read = readRleComment(text, lines.number(), pattern); !read)
            {
                return std::unexpected(read.error());
            }
            continue;
        }
        if (!startsWithIgnoringCase(text, "x"))
        {
            break;
        }
        RleSize size;
        if (const auto read = readHeader(text, lines.number(), pattern, size.width, size.height);
            !read)
        {
            return std::unexpected(read.error());
        }
        return size;
    }
    return fail(PatternErrorKind::NO_HEADER, lines.number());
}

/// Reads the runs of an RLE body one character at a time. A count may be split from its tag by
/// white space, and a row by a line end.
class RleRuns
{
public:
    /// @return true once `c` is the closing '!'.
    [[nodiscard]] std::expected<bool, PatternError> take(char c, int line)
    {
        if (isDigit(c))
        {
            m_count    = (m_count * 10) + (c - '0');
            m_hasCount = true;
            if (m_count > kMaxWorldSide)
            {
                return fail(PatternErrorKind::TOO_LARGE, line);
            }
            return false;
        }
        if (c == ' ' || c == '\t')
        {
            return false;
        }
        const std::int64_t run = m_hasCount ? m_count : 1;
        m_count                = 0;
        m_hasCount             = false;
        switch (c)
        {
            case 'b':
            case '.':
                m_x += run;
                return false;
            case 'o':
            case 'A':
                if (!m_cells.add(m_x, m_y, run))
                {
                    return fail(PatternErrorKind::TOO_LARGE, line);
                }
                m_x += run;
                return false;
            case '$':
                m_y += run;
                m_x = 0;
                return false;
            case '!':
                return true;
            default:
                break;
        }
        // Multi-state RLE: B to X are states 2 to 24, and p to y prefix the higher ones.
        if ((c >= 'B' && c <= 'X') || (c >= 'p' && c <= 'y'))
        {
            return fail(PatternErrorKind::MULTI_STATE, line);
        }
        return fail(PatternErrorKind::BAD_CHARACTER, line, quoteCharacter(c));
    }

    [[nodiscard]] CellCollector& cells() noexcept { return m_cells; }

private:
    CellCollector m_cells;
    std::int64_t  m_x        = 0;
    std::int64_t  m_y        = 0;
    std::int64_t  m_count    = 0;
    bool          m_hasCount = false;
};

[[nodiscard]] std::expected<Pattern, PatternError> readRle(Lines lines)
{
    Pattern                                    pattern;
    const std::expected<RleSize, PatternError> size = readRleHead(lines, pattern);
    if (!size)
    {
        return std::unexpected(size.error());
    }
    RleRuns runs;
    bool    done = false;
    while (!done)
    {
        const std::optional<std::string_view> line = lines.next();
        if (!line)
        {
            break;  // a missing '!' is forgiven
        }
        if (line->starts_with('#'))
        {
            continue;
        }
        for (const char c : *line)
        {
            const std::expected<bool, PatternError> end = runs.take(c, lines.number());
            if (!end)
            {
                return std::unexpected(end.error());
            }
            if (*end)
            {
                done = true;
                break;
            }
        }
    }
    runs.cells().finish(pattern, size->width, size->height);
    return pattern;
}

// A "!" line of a plaintext file, without the '!'.
void readPlaintextComment(std::string_view text, Pattern& pattern)
{
    constexpr std::string_view kName   = "Name:";
    constexpr std::string_view kAuthor = "Author:";
    if (startsWithIgnoringCase(text, kName) && pattern.name.empty())
    {
        pattern.name = trim(text.substr(kName.size()));
    }
    else if (startsWithIgnoringCase(text, kAuthor) && pattern.author.empty())
    {
        pattern.author = trim(text.substr(kAuthor.size()));
    }
    else
    {
        pattern.comments.emplace_back(text);
    }
}

[[nodiscard]] std::expected<Pattern, PatternError> readPlaintext(Lines lines)
{
    Pattern       pattern;
    CellCollector collector;
    std::int64_t  width  = 0;
    std::int64_t  height = 0;  // rows up to the last one that is not empty
    std::int64_t  y      = 0;
    while (const std::optional<std::string_view> line = lines.next())
    {
        if (line->starts_with('!'))
        {
            readPlaintextComment(trim(line->substr(1)), pattern);
            continue;
        }
        const std::string_view row = line->substr(0, line->find_last_not_of(" \t") + 1);
        for (std::size_t x = 0; x < row.size(); ++x)
        {
            const char c = row[x];
            if (c == 'O' || c == '*')
            {
                if (!collector.add(static_cast<std::int64_t>(x), y, 1))
                {
                    return fail(PatternErrorKind::TOO_LARGE, lines.number());
                }
            }
            else if (c != '.')
            {
                return fail(PatternErrorKind::BAD_CHARACTER, lines.number(), quoteCharacter(c));
            }
        }
        if (!row.empty())
        {
            if (std::cmp_greater(row.size(), kMaxWorldSide) || y >= kMaxWorldSide)
            {
                return fail(PatternErrorKind::TOO_LARGE, lines.number());
            }
            width  = std::max(width, static_cast<std::int64_t>(row.size()));
            height = y + 1;
        }
        ++y;
    }
    collector.finish(pattern, width, height);
    return pattern;
}

// The deepest macrocell root the universe holds: 2^62 cells across (kUniverseRadius).
constexpr unsigned kMaxMacrocellLevel = 62;
static_assert(UniverseCoord{1} << (kMaxMacrocellLevel - 1) == kUniverseRadius);
constexpr unsigned kLeafLevel = 3;
constexpr int      kLeafSide  = 8;
// More would overflow the counts; no pattern comes near it.
constexpr std::uint64_t kMaxMacrocellPopulation = std::uint64_t{1} << 62;

// A macrocell node's live cells: how many, and where, relative to its top-left corner.
struct NodeFacts
{
    std::uint64_t population = 0;
    UniverseRect  box;  ///< Meaningless when population is 0.
};

[[nodiscard]] NodeFacts leafFacts(std::uint64_t leaf) noexcept
{
    NodeFacts facts{.population = static_cast<std::uint64_t>(std::popcount(leaf)),
                    .box        = {.x0 = kLeafSide, .y0 = kLeafSide, .x1 = 0, .y1 = 0}};
    for (int bit = 0; bit < kLeafSide * kLeafSide; ++bit)
    {
        if (((leaf >> bit) & 1U) != 0)
        {
            const int x = bit % kLeafSide;
            const int y = bit / kLeafSide;
            facts.box   = {.x0 = std::min<UniverseCoord>(facts.box.x0, x),
                           .y0 = std::min<UniverseCoord>(facts.box.y0, y),
                           .x1 = std::max<UniverseCoord>(facts.box.x1, x + 1),
                           .y1 = std::max<UniverseCoord>(facts.box.y1, y + 1)};
        }
    }
    return facts;
}

// A leaf line: rows of '.' and '*', each ended by '$'.
[[nodiscard]] std::expected<std::uint64_t, PatternError> readLeaf(std::string_view text, int line)
{
    std::uint64_t leaf = 0;
    int           x    = 0;
    int           y    = 0;
    for (const char c : text)
    {
        if (c == '$')
        {
            ++y;
            x = 0;
            continue;
        }
        if (c != '.' && c != '*')
        {
            return fail(PatternErrorKind::BAD_CHARACTER, line, quoteCharacter(c));
        }
        if (x >= kLeafSide || y >= kLeafSide)
        {
            return fail(PatternErrorKind::BAD_NODE, line,
                        "A leaf is wider or taller than 8 cells.");
        }
        if (c == '*')
        {
            leaf |= std::uint64_t{1} << ((y * kLeafSide) + x);
        }
        ++x;
    }
    return leaf;
}

// A node line, "level nw ne sw se", given the nodes before it.
[[nodiscard]] std::expected<MacrocellNode, PatternError> readNode(
    std::string_view text, int line, const std::vector<MacrocellNode>& before)
{
    std::array<std::uint64_t, 5> numbers{};
    std::size_t                  count = 0;
    for (std::string_view rest = text; !rest.empty(); rest = trim(rest))
    {
        std::uint64_t     value = 0;
        const char* const first = std::to_address(rest.begin());
        const char* const last  = std::to_address(rest.end());
        const auto [end, error] = std::from_chars(first, last, value);
        const auto used         = static_cast<std::size_t>(std::distance(first, end));
        if (error != std::errc{} || count == numbers.size() ||
            (used < rest.size() && !kSpace.contains(rest[used])))
        {
            return fail(PatternErrorKind::BAD_NODE, line,
                        std::format("Cannot read the node \"{}\".", text));
        }
        numbers.at(count++) = value;
        rest.remove_prefix(used);
    }
    if (count != numbers.size())
    {
        return fail(PatternErrorKind::BAD_NODE, line,
                    std::format("Cannot read the node \"{}\".", text));
    }
    const std::uint64_t level = numbers[0];
    if (level == 1)
    {
        return fail(PatternErrorKind::UNSUPPORTED_FORMAT, 0, "multi-state macrocell");
    }
    if (level <= kLeafLevel)
    {
        return fail(PatternErrorKind::BAD_NODE, line,
                    std::format("A node of level {} would be no larger than a leaf.", level));
    }
    if (level > kMaxMacrocellLevel)
    {
        return fail(PatternErrorKind::BAD_NODE, line,
                    "The pattern is larger than the universe, 2^62 cells across.");
    }
    MacrocellNode node{.level = static_cast<std::uint8_t>(level), .leaf = 0, .children = {}};
    for (std::size_t i = 0; i < node.children.size(); ++i)
    {
        const std::uint64_t child = numbers.at(i + 1);
        if (child > before.size())
        {
            return fail(
                PatternErrorKind::BAD_NODE, line,
                std::format("A node refers to node {}, which does not come before it.", child));
        }
        if (child != 0 && before.at(child - 1).level != level - 1)
        {
            return fail(PatternErrorKind::BAD_NODE, line,
                        std::format("A node of level {} has a child of level {}.", level,
                                    before.at(child - 1).level));
        }
        node.children.at(i) = static_cast<std::uint32_t>(child);
    }
    return node;
}

// The facts of an inner node from its children's.
[[nodiscard]] std::expected<NodeFacts, PatternError> innerFacts(const MacrocellNode&          node,
                                                                const std::vector<NodeFacts>& all,
                                                                int                           line)
{
    const UniverseCoord half = UniverseCoord{1} << (node.level - 1);
    NodeFacts           facts;
    for (std::size_t i = 0; i < node.children.size(); ++i)
    {
        if (node.children.at(i) == 0)
        {
            continue;
        }
        const NodeFacts& child = all.at(node.children.at(i) - 1);
        if (child.population == 0)
        {
            continue;
        }
        if (child.population > kMaxMacrocellPopulation - facts.population)
        {
            return fail(PatternErrorKind::BAD_NODE, line, "The pattern has too many live cells.");
        }
        const UniverseCoord dx = (i % 2 == 0) ? 0 : half;
        const UniverseCoord dy = (i < 2) ? 0 : half;
        const UniverseRect  box{.x0 = child.box.x0 + dx,
                                .y0 = child.box.y0 + dy,
                                .x1 = child.box.x1 + dx,
                                .y1 = child.box.y1 + dy};
        facts.box = facts.population == 0 ? box
                                          : UniverseRect{.x0 = std::min(facts.box.x0, box.x0),
                                                         .y0 = std::min(facts.box.y0, box.y0),
                                                         .x1 = std::max(facts.box.x1, box.x1),
                                                         .y1 = std::max(facts.box.y1, box.y1)};
        facts.population += child.population;
    }
    return facts;
}

// The comment and setting lines of a macrocell file.
[[nodiscard]] std::expected<void, PatternError> readMacrocellHash(std::string_view text, int line,
                                                                  Pattern& pattern, Macrocell& tree)
{
    const char             kind = text.size() > 1 ? text[1] : ' ';
    const std::string_view rest = text.size() > 2 ? trim(text.substr(2)) : std::string_view{};
    switch (kind)
    {
        case 'R':
        {
            const auto rule = readRule(rest, line);
            if (!rule)
            {
                return std::unexpected(rule.error());
            }
            pattern.rule = *rule;
            break;
        }
        case 'G':
        {
            const char* const first = std::to_address(rest.begin());
            const char* const last  = std::to_address(rest.end());
            const auto [end, error] = std::from_chars(first, last, tree.generation);
            if (error != std::errc{} || end != last)
            {
                return fail(PatternErrorKind::BAD_HEADER, line, std::string(text));
            }
            break;
        }
        case 'N':
            pattern.name = rest;
            break;
        case 'O':
            pattern.author = rest;
            break;
        case 'C':
        case 'c':
            pattern.comments.emplace_back(rest);
            break;
        default:  // Golly's others, such as #FRAMES, say nothing about the pattern
            break;
    }
    return {};
}

[[nodiscard]] std::expected<Pattern, PatternError> readMacrocell(Lines lines)
{
    Pattern                pattern;
    Macrocell              tree;
    std::vector<NodeFacts> facts;
    while (const std::optional<std::string_view> line = lines.next())
    {
        const std::string_view text   = trim(*line);
        const int              number = lines.number();
        if (text.empty() || text.starts_with('['))  // the [M2] line
        {
            continue;
        }
        if (text.starts_with('#'))
        {
            if (auto read = readMacrocellHash(text, number, pattern, tree); !read)
            {
                return std::unexpected(read.error());
            }
            continue;
        }
        if (text.starts_with('.') || text.starts_with('*') || text.starts_with('$'))
        {
            const auto leaf = readLeaf(text, number);
            if (!leaf)
            {
                return std::unexpected(leaf.error());
            }
            tree.nodes.push_back({.level = kLeafLevel, .leaf = *leaf, .children = {}});
            facts.push_back(leafFacts(*leaf));
            continue;
        }
        if (!isDigit(text.front()))
        {
            return fail(PatternErrorKind::BAD_CHARACTER, number, quoteCharacter(text.front()));
        }
        const auto node = readNode(text, number, tree.nodes);
        if (!node)
        {
            return std::unexpected(node.error());
        }
        const auto nodeFacts = innerFacts(*node, facts, number);
        if (!nodeFacts)
        {
            return std::unexpected(nodeFacts.error());
        }
        tree.nodes.push_back(*node);
        facts.push_back(*nodeFacts);
    }
    if (!tree.nodes.empty() && facts.back().population > 0)
    {
        // The root is centred on (0, 0).
        const UniverseCoord half = UniverseCoord{1} << (tree.nodes.back().level - 1);
        const UniverseRect& box  = facts.back().box;
        tree.population          = facts.back().population;
        tree.bounds              = {
            .x0 = box.x0 - half, .y0 = box.y0 - half, .x1 = box.x1 - half, .y1 = box.y1 - half};
    }
    pattern.tree = std::move(tree);
    return pattern;
}

}  // namespace

std::string describe(const PatternError& error)
{
    std::string message = error.line > 0 ? std::format("Line {}: ", error.line) : std::string();
    switch (error.kind)
    {
        case PatternErrorKind::EMPTY:
            message += "The file is empty.";
            break;
        case PatternErrorKind::UNKNOWN_FORMAT:
            message += "This is not an RLE (.rle), plaintext (.cells) or macrocell (.mc) pattern.";
            break;
        case PatternErrorKind::UNSUPPORTED_FORMAT:
            message += std::format(
                "wxLife reads RLE (.rle), plaintext (.cells) and macrocell (.mc) patterns, not {} "
                "files.",
                error.detail);
            break;
        case PatternErrorKind::NO_HEADER:
            message +=
                "An RLE pattern needs a header line such as \"x = 3, y = 3, rule = B3/S23\".";
            break;
        case PatternErrorKind::BAD_HEADER:
            message += std::format("Cannot read the header \"{}\".", error.detail);
            break;
        case PatternErrorKind::UNSUPPORTED_RULE:
            message += std::format("wxLife runs two-state B/S rules such as B3/S23, not \"{}\".",
                                   error.detail);
            break;
        case PatternErrorKind::MULTI_STATE:
            message += "The pattern has more than two cell states.";
            break;
        case PatternErrorKind::BAD_CHARACTER:
            message += std::format("Unexpected character {}.", error.detail);
            break;
        case PatternErrorKind::TOO_LARGE:
            message +=
                error.detail.empty()
                    ? std::format("The pattern is wider or taller than {} cells.",
                                  formatCount(kMaxWorldSide))
                    : std::format("The pattern is {} cells; a world has at most {} cells per side.",
                                  error.detail, formatCount(kMaxWorldSide));
            break;
        case PatternErrorKind::BAD_NODE:
            message += error.detail;
            break;
    }
    return message;
}

std::expected<Pattern, PatternError> readPattern(std::string_view text)
{
    // The first line that is not blank tells the format.
    Lines probe(text);
    while (const std::optional<std::string_view> line = probe.next())
    {
        const std::string_view first = trim(*line);
        if (first.empty())
        {
            continue;
        }
        if (first.starts_with("[M2]"))
        {
            return readMacrocell(Lines(text));
        }
        if (startsWithIgnoringCase(first, "#Life 1."))
        {
            return fail(PatternErrorKind::UNSUPPORTED_FORMAT, 0,
                        std::format("Life {}", trim(first.substr(6))));
        }
        if (first.starts_with('#') ||
            (startsWithIgnoringCase(first, "x") && trim(first.substr(1)).starts_with('=')))
        {
            return readRle(Lines(text));
        }
        if (first.starts_with('!') || first.starts_with('.') || first.starts_with('O') ||
            first.starts_with('*'))
        {
            return readPlaintext(Lines(text));
        }
        return fail(PatternErrorKind::UNKNOWN_FORMAT, 0);
    }
    return fail(PatternErrorKind::EMPTY, 0);
}

std::expected<Pattern, std::string> readPatternData(std::string_view contents)
{
    std::string unpacked;
    if (isGzip(contents))
    {
        std::expected<std::string, std::string> text = gunzip(contents, kMaxPatternBytes);
        if (!text)
        {
            return std::unexpected(std::format("The file cannot be unpacked. {}", text.error()));
        }
        unpacked = std::move(*text);
        contents = unpacked;
    }
    return readPattern(contents).transform_error(
        [](const PatternError& error) { return describe(error); });
}

}  // namespace wxLife::core
