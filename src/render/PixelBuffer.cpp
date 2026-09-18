#include "wxLife/render/PixelBuffer.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "wxLife/render/Types.h"

namespace wxLife::render
{

void PixelBuffer::resize(PixelSize size)
{
    assert(size.width >= 0 && size.height >= 0);
    bytes_.resize(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) *
                  kBytesPerPixel);
    size_ = size;  // only after the allocation succeeded
}

PixelSize PixelBuffer::size() const noexcept
{
    return size_;
}

std::span<std::uint8_t> PixelBuffer::bytes() noexcept
{
    return bytes_;
}

std::span<const std::uint8_t> PixelBuffer::bytes() const noexcept
{
    return bytes_;
}

std::span<std::uint8_t> PixelBuffer::row(Pixel y) noexcept
{
    assert(y >= 0 && y < size_.height);
    const std::size_t rowBytes = static_cast<std::size_t>(size_.width) * kBytesPerPixel;
    return std::span(bytes_).subspan(static_cast<std::size_t>(y) * rowBytes, rowBytes);
}

Rgb PixelBuffer::at(Pixel x, Pixel y) const noexcept
{
    assert(x >= 0 && x < size_.width && y >= 0 && y < size_.height);
    const std::size_t i = static_cast<std::size_t>((y * size_.width) + x) * kBytesPerPixel;
    return {.r = bytes_[i], .g = bytes_[i + 1], .b = bytes_[i + 2]};
}

void PixelBuffer::fill(Rgb color) noexcept
{
    for (std::size_t i = 0; i < bytes_.size(); i += kBytesPerPixel)
    {
        bytes_[i]     = color.r;
        bytes_[i + 1] = color.g;
        bytes_[i + 2] = color.b;
    }
}

}  // namespace wxLife::render
