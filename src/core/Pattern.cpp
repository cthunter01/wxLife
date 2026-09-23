#include "wxLife/core/Pattern.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "wxLife/core/Format.h"
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
            message += "This is neither an RLE (.rle) nor a plaintext (.cells) pattern.";
            break;
        case PatternErrorKind::UNSUPPORTED_FORMAT:
            message += std::format(
                "wxLife reads RLE (.rle) and plaintext (.cells) patterns, not {} files.",
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
            return fail(PatternErrorKind::UNSUPPORTED_FORMAT, 0, "macrocell (.mc)");
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

}  // namespace wxLife::core
