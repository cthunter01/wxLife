#include "wxLife/core/Gzip.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wxLife::core
{
namespace
{

// Thrown inside gunzip() when the data cannot be read; its text goes to the caller.
struct Damaged
{
    std::string_view why;
};

constexpr std::uint8_t kMagic1  = 0x1f;
constexpr std::uint8_t kMagic2  = 0x8b;
constexpr std::uint8_t kDeflate = 8;  // the only compression method gzip defines

// Header flags (RFC 1952, 2.3.1).
constexpr std::uint8_t kFlagHeaderCrc = 0x02;
constexpr std::uint8_t kFlagExtra     = 0x04;
constexpr std::uint8_t kFlagName      = 0x08;
constexpr std::uint8_t kFlagComment   = 0x10;
constexpr std::uint8_t kFlagReserved  = 0xe0;

constexpr std::array<std::uint32_t, 256> kCrcTable = [] {
    constexpr std::uint32_t        kPolynomial = 0xedb88320;  // reflected
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < table.size(); ++n)
    {
        std::uint32_t c = n;
        for (int bit = 0; bit < 8; ++bit)
        {
            c = (c & 1U) != 0 ? kPolynomial ^ (c >> 1U) : c >> 1U;
        }
        table.at(n) = c;
    }
    return table;
}();

// DEFLATE's bits, least significant first, from a byte string.
class BitReader
{
public:
    explicit BitReader(std::string_view data, std::size_t start) : m_data(data), m_next(start) { }

    // The next `n` bits (n <= 32) without taking them; bits past the end read as 0.
    [[nodiscard]] std::uint32_t peek(unsigned n)
    {
        refill();
        return static_cast<std::uint32_t>(m_buffer & ((std::uint64_t{1} << n) - 1));
    }

    void consume(unsigned n)
    {
        refill();
        if (n > m_count)
        {
            throw Damaged{"The data ends too early."};
        }
        m_buffer >>= n;
        m_count -= n;
    }

    [[nodiscard]] std::uint32_t bits(unsigned n)
    {
        const std::uint32_t value = peek(n);
        consume(n);
        return value;
    }

    // Drops the rest of the current byte.
    void alignToByte() { consume(m_count % 8); }

    // The position of the next whole byte. @pre aligned
    [[nodiscard]] std::size_t bytePosition() const noexcept { return m_next - (m_count / 8); }

private:
    void refill() noexcept
    {
        while (m_count <= 56 && m_next < m_data.size())
        {
            m_buffer |= std::uint64_t{static_cast<std::uint8_t>(m_data[m_next])} << m_count;
            m_count += 8;
            ++m_next;
        }
    }

    std::string_view m_data;
    std::size_t      m_next   = 0;  // the next byte to go into the buffer
    std::uint64_t    m_buffer = 0;
    unsigned         m_count  = 0;  // bits in the buffer
};

constexpr unsigned kMaxCodeBits = 15;

// A canonical Huffman code, decoded with one table lookup: the next maxBits bits index the table,
// whose entry holds the symbol and the length of its code.
class Huffman
{
public:
    // `lengths` holds each symbol's code length; 0 leaves the symbol out. A code may be incomplete
    // (the codes nobody uses are then invalid), but not over-subscribed.
    void build(std::span<const std::uint8_t> lengths)
    {
        std::array<int, kMaxCodeBits + 1> count{};
        for (const std::uint8_t length : lengths)
        {
            ++count.at(length);
        }
        count[0] = 0;
        int left = 1;  // codes still free at the current length
        for (unsigned length = 1; length <= kMaxCodeBits; ++length)
        {
            left = (left * 2) - count.at(length);
            if (left < 0)
            {
                throw Damaged{"A Huffman code in the data is invalid."};
            }
        }
        m_maxBits = 1;
        for (unsigned length = kMaxCodeBits; length > 1; --length)
        {
            if (count.at(length) != 0)
            {
                m_maxBits = length;
                break;
            }
        }
        std::array<std::uint32_t, kMaxCodeBits + 2> next{};
        for (unsigned length = 1; length <= kMaxCodeBits; ++length)
        {
            next.at(length + 1) = (next.at(length) + static_cast<std::uint32_t>(count.at(length)))
                                  << 1U;
        }
        m_table.assign(std::size_t{1} << m_maxBits, 0);
        for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol)
        {
            const unsigned length = lengths[symbol];
            if (length == 0)
            {
                continue;
            }
            // Codes are stored most significant bit first, but read least significant first.
            const std::uint32_t code     = next.at(length)++;
            std::uint32_t       reversed = 0;
            for (unsigned bit = 0; bit < length; ++bit)
            {
                reversed |= ((code >> bit) & 1U) << (length - 1 - bit);
            }
            const auto entry = static_cast<std::uint16_t>((symbol << 4U) | length);
            for (std::size_t index = reversed; index < m_table.size();
                 index += std::size_t{1} << length)
            {
                m_table[index] = entry;
            }
        }
    }

    [[nodiscard]] unsigned decode(BitReader& in) const
    {
        const std::uint16_t entry  = m_table[in.peek(m_maxBits)];
        const unsigned      length = entry & 0xfU;
        if (length == 0)
        {
            throw Damaged{"The data holds a code that its Huffman table does not have."};
        }
        in.consume(length);
        return entry >> 4U;
    }

private:
    std::vector<std::uint16_t> m_table;
    unsigned                   m_maxBits = 1;
};

constexpr unsigned kEndOfBlock    = 256;
constexpr unsigned kLengthCodes   = 29;
constexpr unsigned kDistanceCodes = 30;

constexpr std::array<std::uint16_t, kLengthCodes> kLengthBase{
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, kLengthCodes> kLengthExtra{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, kDistanceCodes> kDistanceBase{
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, kDistanceCodes> kDistanceExtra{
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// The order in which a dynamic block lists the lengths of the code-length code.
constexpr std::array<std::uint8_t, 19> kCodeLengthOrder{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                        11, 4,  12, 3, 13, 2, 14, 1, 15};

// Appends one block's symbols to `out`.
void inflateBlock(BitReader& in, const Huffman& literals, const Huffman& distances,
                  std::string& out, std::size_t maxSize)
{
    for (;;)
    {
        const unsigned symbol = literals.decode(in);
        if (symbol < kEndOfBlock)
        {
            if (out.size() == maxSize)
            {
                throw Damaged{};
            }
            out.push_back(static_cast<char>(symbol));
            continue;
        }
        if (symbol == kEndOfBlock)
        {
            return;
        }
        const unsigned lengthCode = symbol - kEndOfBlock - 1;
        if (lengthCode >= kLengthCodes)
        {
            throw Damaged{"The data holds an invalid length."};
        }
        const std::size_t length =
            kLengthBase.at(lengthCode) + in.bits(kLengthExtra.at(lengthCode));
        const unsigned distanceCode = distances.decode(in);
        if (distanceCode >= kDistanceCodes)
        {
            throw Damaged{"The data holds an invalid distance."};
        }
        const std::size_t distance =
            kDistanceBase.at(distanceCode) + in.bits(kDistanceExtra.at(distanceCode));
        if (distance > out.size())
        {
            throw Damaged{"The data refers to bytes before its start."};
        }
        if (length > maxSize - out.size())
        {
            throw Damaged{};
        }
        const std::size_t start = out.size();
        out.resize(start + length);
        // The copy may overlap what it writes: a distance shorter than the length repeats.
        for (std::size_t i = start; i < start + length; ++i)
        {
            out[i] = out[i - distance];
        }
    }
}

void inflateStored(BitReader& in, std::string& out, std::size_t maxSize)
{
    in.alignToByte();
    const std::uint32_t length = in.bits(16);
    const std::uint32_t check  = in.bits(16);
    if ((length ^ check) != 0xffffU)
    {
        throw Damaged{"A stored block in the data is damaged."};
    }
    if (length > maxSize - out.size())
    {
        throw Damaged{};
    }
    for (std::uint32_t i = 0; i < length; ++i)
    {
        out.push_back(static_cast<char>(in.bits(8)));
    }
}

void readDynamicTables(BitReader& in, Huffman& literals, Huffman& distances)
{
    const unsigned literalCount  = in.bits(5) + 257;
    const unsigned distanceCount = in.bits(5) + 1;
    const unsigned lengthCount   = in.bits(4) + 4;
    if (literalCount > kEndOfBlock + 1 + kLengthCodes || distanceCount > kDistanceCodes)
    {
        throw Damaged{"A block header in the data is invalid."};
    }
    std::array<std::uint8_t, kCodeLengthOrder.size()> codeLengthLengths{};
    for (unsigned i = 0; i < lengthCount; ++i)
    {
        codeLengthLengths.at(kCodeLengthOrder.at(i)) = static_cast<std::uint8_t>(in.bits(3));
    }
    Huffman codeLengths;
    codeLengths.build(codeLengthLengths);

    // Both tables' lengths come in one run, and a repeat may cross from one into the other.
    std::vector<std::uint8_t> lengths;
    lengths.reserve(literalCount + distanceCount);
    while (lengths.size() < literalCount + distanceCount)
    {
        const unsigned symbol = codeLengths.decode(in);
        std::size_t    repeat = 1;
        std::uint8_t   value  = 0;
        if (symbol < 16)
        {
            value = static_cast<std::uint8_t>(symbol);
        }
        else if (symbol == 16)
        {
            if (lengths.empty())
            {
                throw Damaged{"A block header in the data repeats a length it has not given."};
            }
            value  = lengths.back();
            repeat = 3 + in.bits(2);
        }
        else if (symbol == 17)
        {
            repeat = 3 + in.bits(3);
        }
        else
        {
            repeat = 11 + in.bits(7);
        }
        if (lengths.size() + repeat > literalCount + distanceCount)
        {
            throw Damaged{"A block header in the data gives too many lengths."};
        }
        lengths.insert(lengths.end(), repeat, value);
    }
    if (lengths.at(kEndOfBlock) == 0)
    {
        throw Damaged{"A block in the data has no end."};
    }
    const std::span<const std::uint8_t> all(lengths);
    literals.build(all.first(literalCount));
    distances.build(all.subspan(literalCount));
}

// The fixed codes of RFC 1951, 3.2.6.
void buildFixedTables(Huffman& literals, Huffman& distances)
{
    std::array<std::uint8_t, 288> literalLengths{};
    const std::span<std::uint8_t> all(literalLengths);
    std::ranges::fill(all.subspan(0, 144), std::uint8_t{8});
    std::ranges::fill(all.subspan(144, 112), std::uint8_t{9});
    std::ranges::fill(all.subspan(256, 24), std::uint8_t{7});
    std::ranges::fill(all.subspan(280, 8), std::uint8_t{8});
    literals.build(literalLengths);
    std::array<std::uint8_t, kDistanceCodes> distanceLengths{};
    distanceLengths.fill(5);
    distances.build(distanceLengths);
}

// Unpacks one DEFLATE stream starting at byte `start`, appending to `out`. @return the byte after
// its end.
std::size_t inflate(std::string_view data, std::size_t start, std::string& out, std::size_t maxSize)
{
    BitReader in(data, start);
    Huffman   literals;
    Huffman   distances;
    bool      last = false;
    while (!last)
    {
        last = in.bits(1) == 1;
        switch (in.bits(2))
        {
            case 0:
                inflateStored(in, out, maxSize);
                break;
            case 1:
                buildFixedTables(literals, distances);
                inflateBlock(in, literals, distances, out, maxSize);
                break;
            case 2:
                readDynamicTables(in, literals, distances);
                inflateBlock(in, literals, distances, out, maxSize);
                break;
            default:
                throw Damaged{"The data holds a block of an unknown kind."};
        }
    }
    in.alignToByte();
    return in.bytePosition();
}

std::uint32_t littleEndian32(std::string_view data, std::size_t at)
{
    if (at + 4 > data.size())
    {
        throw Damaged{"The data ends too early."};
    }
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        value |= std::uint32_t{static_cast<std::uint8_t>(data[at + i])} << (8 * i);
    }
    return value;
}

// The position of the DEFLATE stream in the member that starts at `at`.
std::size_t skipHeader(std::string_view data, std::size_t at)
{
    const auto byte = [&](std::size_t i) {
        if (i >= data.size())
        {
            throw Damaged{"The data ends too early."};
        }
        return static_cast<std::uint8_t>(data[i]);
    };
    if (byte(at) != kMagic1 || byte(at + 1) != kMagic2)
    {
        throw Damaged{"The data is not gzip."};
    }
    if (byte(at + 2) != kDeflate)
    {
        throw Damaged{"The data uses an unknown compression method."};
    }
    const std::uint8_t flags = byte(at + 3);
    if ((flags & kFlagReserved) != 0)
    {
        throw Damaged{"The gzip header sets flags that do not exist."};
    }
    std::size_t next = at + 10;  // magic, method, flags, time, extra flags, system
    if ((flags & kFlagExtra) != 0)
    {
        next += 2 + (std::size_t{byte(next)} | (std::size_t{byte(next + 1)} << 8U));
    }
    for (const std::uint8_t text : {kFlagName, kFlagComment})
    {
        if ((flags & text) != 0)
        {
            while (byte(next) != 0)  // zero-terminated
            {
                ++next;
            }
            ++next;
        }
    }
    if ((flags & kFlagHeaderCrc) != 0)
    {
        next += 2;
    }
    if (next > data.size())
    {
        throw Damaged{"The data ends too early."};
    }
    return next;
}

}  // namespace

bool isGzip(std::string_view data) noexcept
{
    return data.size() >= 2 && static_cast<std::uint8_t>(data[0]) == kMagic1 &&
           static_cast<std::uint8_t>(data[1]) == kMagic2;
}

std::expected<std::string, std::string> gunzip(std::string_view data, std::size_t maxSize)
{
    std::string out;
    // The last four bytes are the length of the last member: usually the whole. DEFLATE packs at
    // most 1032 bytes into one, which bounds what a damaged trailer can ask for.
    constexpr std::size_t kMaxRatio = 1032;
    if (isGzip(data) && data.size() >= 4)
    {
        const std::size_t claimed = littleEndian32(data, data.size() - 4);
        out.reserve(std::min({claimed, maxSize, data.size() * kMaxRatio}));
    }
    try
    {
        std::size_t at = 0;
        // Some tools pad the end with zeros.
        const auto onlyPadding = [&data](std::size_t from) {
            return std::ranges::all_of(data.substr(from), [](char c) { return c == 0; });
        };
        while (at == 0 || (at < data.size() && !onlyPadding(at)))
        {
            const std::size_t      first  = out.size();
            const std::size_t      end    = inflate(data, skipHeader(data, at), out, maxSize);
            const std::string_view member = std::string_view(out).substr(first);
            if (littleEndian32(data, end) != crc32(member))
            {
                throw Damaged{"The data is damaged: its checksum does not match."};
            }
            if (littleEndian32(data, end + 4) != static_cast<std::uint32_t>(member.size()))
            {
                throw Damaged{"The data is damaged: its length does not match."};
            }
            at = end + 8;
        }
    }
    catch (const Damaged& damaged)
    {
        if (damaged.why.empty())
        {
            return std::unexpected(std::format("The data unpacks to more than {} bytes.", maxSize));
        }
        return std::unexpected(std::string(damaged.why));
    }
    return out;
}

std::uint32_t crc32(std::string_view data, std::uint32_t crc) noexcept
{
    crc = ~crc;
    for (const char c : data)
    {
        crc = kCrcTable.at((crc ^ static_cast<std::uint8_t>(c)) & 0xffU) ^ (crc >> 8U);
    }
    return ~crc;
}

}  // namespace wxLife::core
