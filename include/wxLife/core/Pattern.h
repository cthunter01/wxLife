/// @file
/// Pattern files: RLE and plaintext (.cells), read into a list of live cells.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wxLife/core/Rule.h"
#include "wxLife/core/Types.h"

namespace wxLife::core
{

/// A pattern read from a file: its live cells, relative to the top-left corner of its bounding box,
/// and what the file says about it.
struct Pattern
{
    std::string              name;      ///< RLE "#N", plaintext "!Name:"; may be empty.
    std::string              author;    ///< RLE "#O", plaintext "!Author:"; may be empty.
    std::vector<std::string> comments;  ///< The other comment lines, without their marker.
    std::optional<Rule>      rule;      ///< nullopt when the file names none.
    /// The header's size (RLE) or the text's (plaintext), grown to contain every live cell. At
    /// most kMaxWorldSide on each side.
    Extent extent;
    /// Live cells inside `extent`, row by row from the top, each row from the left.
    std::vector<CellPos> cells;
};

/// Why readPattern() rejected a text.
enum class PatternErrorKind : std::uint8_t
{
    EMPTY,               ///< Nothing but white space.
    UNKNOWN_FORMAT,      ///< Neither RLE nor plaintext.
    UNSUPPORTED_FORMAT,  ///< A known format wxLife does not read; `detail` names it.
    NO_HEADER,           ///< RLE without its "x = …, y = …" line.
    BAD_HEADER,          ///< An RLE header that cannot be read; `detail` quotes it.
    UNSUPPORTED_RULE,    ///< Not a two-state B/S rule; `detail` quotes it.
    MULTI_STATE,         ///< RLE cells of a third state.
    BAD_CHARACTER,       ///< `detail` quotes it.
    TOO_LARGE,           ///< Wider or taller than kMaxWorldSide.
};

/// What readPattern() could not read, and where.
struct PatternError
{
    PatternErrorKind kind = PatternErrorKind::EMPTY;
    int              line = 0;  ///< 1-based line of the problem; 0 when no one line is to blame.
    std::string      detail;

    friend bool operator==(const PatternError&, const PatternError&) = default;
};

/// Message for the user, e.g. "Line 4: Unexpected character '%'."
[[nodiscard]] std::string describe(const PatternError& error);

/// Reads an RLE or plaintext (.cells) pattern, recognising the format from the text.
/// - RLE: `#N`, `#O`, `#C`/`#c` and `#r` lines, the `x = …, y = …, rule = …` header, then runs of
///   `b`/`.` (dead), `o`/`A` (alive) and `$` (end of row) up to `!`. White space may appear between
///   runs, and text after `!` is ignored.
/// - Plaintext: `!` comment lines (`!Name:` and `!Author:` fill in those fields), then one line per
///   row of `.` (dead) and `O` or `*` (alive).
///
/// Both accept LF and CRLF line ends. A rule is a B/S rule in any form Rule::parse() takes, or
/// "Life"; Golly's bounded-grid suffix (":T100,100") is ignored.
[[nodiscard]] std::expected<Pattern, PatternError> readPattern(std::string_view text);

}  // namespace wxLife::core
