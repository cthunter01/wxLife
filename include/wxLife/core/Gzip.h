/// @file
/// Reading gzip data (RFC 1952) with a DEFLATE decompressor (RFC 1951) of its own, so neither the
/// embedded patterns nor File → Open need a compression library.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace wxLife::core
{

/// Whether `data` starts like gzip data: the two magic bytes 1f 8b.
[[nodiscard]] bool isGzip(std::string_view data) noexcept;

/// The bytes gzip data holds. Several members, one after the other, are joined. Each member is
/// checked against its CRC-32 and length.
/// @return a message saying why when the data is not gzip, is damaged or cut short, or would
///         unpack to more than `maxSize` bytes.
[[nodiscard]] std::expected<std::string, std::string> gunzip(std::string_view data,
                                                             std::size_t      maxSize);

/// CRC-32 as gzip uses it (the IEEE 802.3 polynomial), continued from `crc`.
[[nodiscard]] std::uint32_t crc32(std::string_view data, std::uint32_t crc = 0) noexcept;

}  // namespace wxLife::core
