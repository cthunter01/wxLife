#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "wxLife/render/Types.h"

namespace wxLife::render
{

/// 24-bit RGB image, row-major, three bytes per pixel: the byte layout of wxImage::GetData().
class PixelBuffer
{
public:
    static constexpr std::size_t kBytesPerPixel = 3;

    /// Contents are unspecified afterwards.
    void                                        resize(PixelSize size);
    [[nodiscard]] PixelSize                     size() const noexcept;
    [[nodiscard]] std::span<std::uint8_t>       bytes() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
    /// 3 × width bytes. @pre 0 <= y < height
    [[nodiscard]] std::span<std::uint8_t> row(Pixel y) noexcept;
    /// @pre the pixel is inside the buffer
    [[nodiscard]] Rgb at(Pixel x, Pixel y) const noexcept;
    void              fill(Rgb color) noexcept;

private:
    PixelSize                 size_{};
    std::vector<std::uint8_t> bytes_;
};

}  // namespace wxLife::render
