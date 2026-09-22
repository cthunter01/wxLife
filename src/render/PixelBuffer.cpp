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
    m_bytes.resize(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) *
                   kBytesPerPixel);
    m_size = size;  // only after the allocation succeeded
}

PixelSize PixelBuffer::size() const noexcept
{
    return m_size;
}

std::span<std::uint8_t> PixelBuffer::bytes() noexcept
{
    return m_bytes;
}

std::span<const std::uint8_t> PixelBuffer::bytes() const noexcept
{
    return m_bytes;
}

std::span<std::uint8_t> PixelBuffer::row(Pixel y) noexcept
{
    assert(y >= 0 && y < m_size.height);
    const std::size_t rowBytes = static_cast<std::size_t>(m_size.width) * kBytesPerPixel;
    return std::span(m_bytes).subspan(static_cast<std::size_t>(y) * rowBytes, rowBytes);
}

Rgb PixelBuffer::at(Pixel x, Pixel y) const noexcept
{
    assert(x >= 0 && x < m_size.width && y >= 0 && y < m_size.height);
    const std::size_t i = static_cast<std::size_t>((y * m_size.width) + x) * kBytesPerPixel;
    return {.r = m_bytes[i], .g = m_bytes[i + 1], .b = m_bytes[i + 2]};
}

void PixelBuffer::fill(Rgb color) noexcept
{
    for (std::size_t i = 0; i < m_bytes.size(); i += kBytesPerPixel)
    {
        m_bytes[i]     = color.r;
        m_bytes[i + 1] = color.g;
        m_bytes[i + 2] = color.b;
    }
}

}  // namespace wxLife::render
